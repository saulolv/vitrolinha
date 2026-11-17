/**
 * @file library.c
 * @brief A biblioteca publicada e a matriz de falhas do cartão.
 *
 * Três leitores com exigências diferentes moldam a sincronização daqui:
 *
 * - O **`player`** não pode bloquear. Tudo o que ele precisa para escolher a
 *   faixa seguinte cabe em 32 bits — uma máscara das faixas tocáveis —, e
 *   por isso a máscara é um atômico, lido sem trava.
 * - A **`ui`** precisa da lista inteira, com nomes. Ela a copia sob
 *   @ref pub_lock, que só é segurada pelo tempo de copiar 32 entradas.
 * - A **`loader`** precisa que o índice que carrega signifique a mesma faixa
 *   do começo ao fim da carga. @ref io_lock serializa a carga com a
 *   varredura inteira — inclusive com a publicação que vem depois dela, que
 *   é o que a trava do volume do `card` sozinha não cobre.
 *
 * A ordem das travas é sempre @ref io_lock antes de @ref pub_lock.
 *
 * @see library.h para o contrato, docs/adr/0010 para as decisões
 */

#include <vitrolinha/library.h>
#include <vitrolinha/storage.h>
#include <vitrolinha/track.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(library, LOG_LEVEL_INF);

/* Uma faixa por bit da máscara de faixas tocáveis. */
BUILD_ASSERT(LIBRARY_MAX <= 32U, "a mascara de faixas validas tem 32 bits");

/**
 * @brief Lista publicada. Só é tocada sob @ref pub_lock.
 *
 * O `valid` de cada entrada e o bit correspondente de @ref lib_valid_mask
 * dizem a mesma coisa; os dois mudam juntos, sob a trava.
 */
static struct track_meta lib_tracks[LIBRARY_MAX];

/** @brief Entradas válidas de @ref lib_tracks. Sob @ref pub_lock. */
static size_t lib_count;

/** @brief Arquivos além do teto na última varredura. Sob @ref pub_lock. */
static size_t lib_skipped;

/**
 * @brief Cópia de trabalho da varredura. Só é tocada sob @ref io_lock.
 *
 * Existe para que a `ui` não espere uma varredura: o cartão é lido para cá,
 * fora de @ref pub_lock, e a publicação é uma cópia de memória. São 896 B
 * fixos (RNF06) que compram uma tela que não congela num cartão lento.
 */
static struct track_meta scan_tracks[LIBRARY_MAX];

/**
 * @brief Faixas tocáveis, um bit por índice.
 *
 * Zero fora de ::LIBRARY_READY, e é isso que desliga seguinte e anterior: o
 * `player` só lê este valor e não precisa saber do estado.
 */
static atomic_t lib_valid_mask = ATOMIC_INIT(0);

/** @brief Um valor de @ref library_state. */
static atomic_t lib_state = ATOMIC_INIT(LIBRARY_NO_CARD);

/** @brief Ver @ref library_generation. Só muda sob @ref pub_lock. */
static atomic_t lib_generation = ATOMIC_INIT(0);

/**
 * @brief Algum cartão já foi lido desde o boot?
 *
 * É a memória que separa "sem cartão" de "cartão removido" e "ilegível" de
 * "reinicie a placa". Sob @ref pub_lock.
 */
static bool lib_had_card;

/** @brief Serializa varredura e carga. Ver o cabeçalho do arquivo. */
static K_MUTEX_DEFINE(io_lock);

/** @brief Protege a lista publicada. Ver o cabeçalho do arquivo. */
static K_MUTEX_DEFINE(pub_lock);

/**
 * @brief Máscara das faixas válidas de uma lista.
 *
 * @param tracks Lista.
 * @param count  Entradas em @p tracks.
 * @return Um bit por faixa válida.
 */
static atomic_val_t valid_mask_of(const struct track_meta *tracks, size_t count)
{
	atomic_val_t mask = 0;

	for (size_t i = 0U; i < count; i++) {
		if (tracks[i].valid) {
			mask |= (atomic_val_t)BIT(i);
		}
	}

	return mask;
}

/**
 * @brief Estado ao qual leva uma falha de cartão.
 *
 * Chamada sob @ref pub_lock, que protege @ref lib_had_card.
 *
 * @param err Erro de @ref storage_scan ou @ref storage_load.
 * @return O estado novo.
 */
static enum library_state state_after(int err)
{
	if (err == -ENODEV) {
		return lib_had_card ? LIBRARY_REMOVED : LIBRARY_NO_CARD;
	}

	/* Todo o resto é cartão presente que não lê. Se algum cartão já leu
	 * nesta sessão, o que falhou foi a remontagem a quente, e insistir
	 * não adianta (zephyr#94033).
	 */
	return lib_had_card ? LIBRARY_REBOOT : LIBRARY_UNREADABLE;
}

/**
 * @brief Publica um estado de falha: lista vazia, transporte desligado.
 *
 * Chamada sob @ref io_lock.
 *
 * @param err Erro de cartão que levou até aqui.
 */
static void publish_failure(int err)
{
	enum library_state state;

	(void)k_mutex_lock(&pub_lock, K_FOREVER);

	/* A máscara antes do estado: quem lê os dois sem trava nunca vê
	 * ::LIBRARY_READY com faixas que já não existem — só, no pior caso,
	 * transporte desligado um instante antes da tela mudar.
	 */
	(void)atomic_set(&lib_valid_mask, 0);

	state = state_after(err);
	lib_count = 0U;
	lib_skipped = 0U;
	(void)atomic_set(&lib_state, (atomic_val_t)state);
	(void)atomic_inc(&lib_generation);

	k_mutex_unlock(&pub_lock);

	LOG_WRN("Biblioteca: %s (%d)", library_state_text(state), err);
}

/**
 * @brief Publica a lista recém-varrida.
 *
 * Chamada sob @ref io_lock, que protege @ref scan_tracks.
 *
 * @param count   Faixas em @ref scan_tracks.
 * @param skipped Arquivos além do teto.
 */
static void publish_tracks(size_t count, size_t skipped)
{
	(void)k_mutex_lock(&pub_lock, K_FOREVER);

	(void)memcpy(lib_tracks, scan_tracks, count * sizeof(lib_tracks[0]));
	lib_count = count;
	lib_skipped = skipped;
	lib_had_card = true;
	(void)atomic_set(&lib_valid_mask, valid_mask_of(lib_tracks, count));
	(void)atomic_set(&lib_state, LIBRARY_READY);
	(void)atomic_inc(&lib_generation);

	k_mutex_unlock(&pub_lock);
}

int library_refresh(void)
{
	size_t skipped;
	int result;

	(void)k_mutex_lock(&io_lock, K_FOREVER);

	result = storage_scan(scan_tracks, ARRAY_SIZE(scan_tracks), &skipped);

	if (result < 0) {
		publish_failure(result);
	} else {
		publish_tracks((size_t)result, skipped);
	}

	k_mutex_unlock(&io_lock);

	return result;
}

enum library_state library_state(void)
{
	return (enum library_state)atomic_get(&lib_state);
}

uint32_t library_generation(void)
{
	return (uint32_t)atomic_get(&lib_generation);
}

int library_copy(struct track_meta *out, size_t max, size_t *skipped)
{
	size_t count;

	if (skipped != NULL) {
		*skipped = 0U;
	}

	if ((out == NULL) || (max == 0U)) {
		return -EINVAL;
	}

	(void)k_mutex_lock(&pub_lock, K_FOREVER);

	count = MIN(max, lib_count);
	(void)memcpy(out, lib_tracks, count * sizeof(out[0]));

	if (skipped != NULL) {
		*skipped = lib_skipped;
	}

	k_mutex_unlock(&pub_lock);

	return (int)count;
}

int library_neighbor(int track, bool forward)
{
	uint32_t mask = (uint32_t)atomic_get(&lib_valid_mask);
	int step = forward ? 1 : -1;
	int index;

	if (mask == 0U) {
		return TRACK_NONE;
	}

	/* Sem faixa de partida, ou com uma que já não cabe na lista, começa
	 * de uma ponta: a primeira volta do laço cai na faixa 0 para frente e
	 * na última para trás.
	 */
	if ((track < 0) || (track >= (int)LIBRARY_MAX)) {
		track = forward ? -1 : (int)LIBRARY_MAX;
	}

	/* Termina em no máximo uma volta inteira: a máscara não é zero, então
	 * alguma posição responde — no pior caso a própria @p track.
	 */
	index = track;

	do {
		index = (index + step + (int)LIBRARY_MAX) % (int)LIBRARY_MAX;
	} while ((mask & BIT(index)) == 0U);

	return index;
}

/**
 * @brief Põe o `!` numa faixa. Chamada sob @ref pub_lock.
 *
 * @param track Índice já conferido contra @ref lib_count.
 */
static void mark_invalid(int track)
{
	lib_tracks[track].valid = false;
	(void)atomic_and(&lib_valid_mask, ~(atomic_val_t)BIT(track));

	LOG_WRN("Faixa %d marcada invalida", track);
}

/**
 * @brief Reage a uma carga que falhou.
 *
 * Chamada sob @ref io_lock.
 *
 * @param track Faixa pedida.
 * @param err   Erro de @ref storage_load.
 */
static void classify_load_error(int track, int err)
{
	switch (err) {
	case -EFBIG:
	case -ENOENT:
		/* Culpa do arquivo: a biblioteca continua de pé. */
		(void)k_mutex_lock(&pub_lock, K_FOREVER);
		mark_invalid(track);
		k_mutex_unlock(&pub_lock);
		break;
	case -ENODEV:
	case -EIO:
		/* Culpa do cartão: a lista inteira deixa de valer. O monitor
		 * chegaria à mesma conclusão na próxima sondagem; publicar
		 * agora desliga o transporte antes que alguém aperte
		 * "seguinte" e pague outra carga que vai falhar.
		 */
		publish_failure(err);
		break;
	default:
		/* Fora do contrato do storage. Não há o que concluir. */
		break;
	}
}

int library_load(int track, uint8_t *buf, size_t len, uint32_t *generation)
{
	enum library_state state;
	int result;

	if ((buf == NULL) || (len < STORAGE_FILE_MAX)) {
		return -EINVAL;
	}

	(void)k_mutex_lock(&io_lock, K_FOREVER);

	/* Lidos sob io_lock: nenhuma publicação acontece fora dela, então o
	 * estado e a geração não mudam até a carga terminar.
	 */
	state = library_state();

	if (generation != NULL) {
		*generation = library_generation();
	}

	if (state != LIBRARY_READY) {
		/* Responde pelo que já se sabe, sem tocar no cartão. */
		result = ((state == LIBRARY_NO_CARD) || (state == LIBRARY_REMOVED)) ? -ENODEV
										    : -EIO;
	} else if ((track < 0) || ((size_t)track >= lib_count)) {
		result = -ENOENT;
	} else {
		result = storage_load(track, buf, len);

		if (result < 0) {
			classify_load_error(track, result);
		}
	}

	k_mutex_unlock(&io_lock);

	return result;
}

int library_reject(int track, uint32_t generation)
{
	int result = 0;

	(void)k_mutex_lock(&pub_lock, K_FOREVER);

	if (generation != library_generation()) {
		result = -ESTALE;
	} else if ((track < 0) || ((size_t)track >= lib_count)) {
		result = -ENOENT;
	} else {
		mark_invalid(track);
	}

	k_mutex_unlock(&pub_lock);

	return result;
}

const char *library_state_text(enum library_state state)
{
	switch (state) {
	case LIBRARY_NO_CARD:
		return "Sem cartao";
	case LIBRARY_READY:
		return "Cartao pronto";
	case LIBRARY_REMOVED:
		return "Cartao removido";
	case LIBRARY_UNREADABLE:
		return "Cartao ilegivel";
	case LIBRARY_REBOOT:
		return "Reinicie a placa";
	default:
		return "Erro inesperado";
	}
}

void library_reset(void)
{
	(void)k_mutex_lock(&io_lock, K_FOREVER);
	(void)k_mutex_lock(&pub_lock, K_FOREVER);

	(void)atomic_set(&lib_valid_mask, 0);
	(void)atomic_set(&lib_state, LIBRARY_NO_CARD);
	(void)atomic_inc(&lib_generation);
	lib_count = 0U;
	lib_skipped = 0U;
	lib_had_card = false;

	k_mutex_unlock(&pub_lock);
	k_mutex_unlock(&io_lock);
}

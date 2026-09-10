/**
 * @file snapshot.c
 * @brief Implementação de @ref snapshot.h.
 */

#include <vitrolinha/seqlock.h>
#include <vitrolinha/snapshot.h>

/**
 * @brief O registro compartilhado.
 *
 * Estático de propósito: não há como um módulo pegar um ponteiro para ele e
 * escrever por fora da disciplina de publicação. A inicialização estática
 * dispensa uma função de init — ::TRACK_NONE não é zero, então zerar a
 * memória não bastaria.
 */
static struct player_snapshot shared = SNAPSHOT_IDLE_INITIALIZER;

/** @brief Serializa a publicação contra a leitura. */
static struct seqlock shared_lock = SEQLOCK_INITIALIZER;

void snapshot_publish(const struct player_snapshot *in)
{
	unsigned int key;

	if (in == NULL) {
		return;
	}

	key = seqlock_write_lock(&shared_lock);
	shared = *in;
	seqlock_write_unlock(&shared_lock, key);
}

void snapshot_read(struct player_snapshot *out)
{
	uint32_t begin;

	if (out == NULL) {
		return;
	}

	/* A cópia tem de ficar dentro do laço: é ela que pode sair rasgada. */
	do {
		begin = seqlock_read_begin(&shared_lock);
		*out = shared;
	} while (seqlock_read_retry(&shared_lock, begin));
}

uint32_t snapshot_elapsed_us(const struct player_snapshot *snap, uint64_t now_us)
{
	uint64_t elapsed;

	if (snap == NULL) {
		return 0U;
	}

	if (snap->state == (uint32_t)PLAYER_PLAYING) {
		/* Saturação em zero: a interface pode ler um instantâneo
		 * publicado com base no futuro imediato, entre o agendamento
		 * de uma nota e o instante dela.
		 */
		elapsed = (now_us > snap->base_us) ? (now_us - snap->base_us) : 0U;
	} else {
		/* Pausado ou parado: base_us guarda o decorrido congelado.
		 * Fim de faixa chega aqui com base_us == total_us, que é o
		 * que põe a barra em 100%.
		 */
		elapsed = snap->base_us;
	}

	if (elapsed > (uint64_t)snap->total_us) {
		elapsed = (uint64_t)snap->total_us;
	}

	return (uint32_t)elapsed;
}

uint16_t snapshot_progress_permille(const struct player_snapshot *snap, uint64_t now_us)
{
	uint32_t elapsed;

	if ((snap == NULL) || (snap->total_us == 0U)) {
		return 0U;
	}

	elapsed = snapshot_elapsed_us(snap, now_us);

	/* Em 64 bits porque `elapsed * 1000` estoura uint32_t a partir de
	 * ~4,3 s de faixa — e as faixas duram minutos.
	 */
	return (uint16_t)(((uint64_t)elapsed * 1000U) / (uint64_t)snap->total_us);
}

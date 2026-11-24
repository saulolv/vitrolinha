/**
 * @file last_track.c
 * @brief Implementação de @ref last_track.h.
 *
 * A trava @ref lt_lock protege o registro gravado e a cópia da lista, que a
 * thread de gravação e quem restaura (o `main`, o monitor do cartão)
 * dividem. Nenhuma das duas é o `player`.
 */
#include <vitrolinha/last_track.h>
#include <vitrolinha/library.h>
#include <vitrolinha/snapshot.h>
#include <vitrolinha/track.h>

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(last_track, LOG_LEVEL_INF);

/**
 * @brief Pilha da thread: cabe a pilha do NVS na escrita. O número
 *        definitivo sai do `CONFIG_THREAD_ANALYZER`, como o das demais.
 */
#define LAST_TRACK_STACK_SIZE 1536

/**
 * @brief Prioridade 13: abaixo do monitor do cartão (12), acima só do log.
 *
 * Gravar a última faixa nunca é urgente.
 */
#define LAST_TRACK_PRIORITY 13

/** @brief O que vai para a flash: a faixa e o nome que a reconhece. */
struct last_track_record {
	uint8_t index;
	char name[TRACK_NAME_MAX];
};

K_MUTEX_DEFINE(lt_lock);

/** @brief Último registro na flash. Sob @ref lt_lock. */
static struct last_track_record stored;
static bool stored_valid;

/** @brief Cópia de trabalho da lista: estática para não pesar na pilha. */
static struct track_meta tracks[LIBRARY_MAX];

/** @brief @ref last_track_restore já respondeu neste boot. */
static atomic_t restored;

/**
 * @brief @ref last_track_init já leu a flash.
 *
 * O monitor do cartão pode chamar @ref last_track_restore antes disso; sem
 * esta guarda a resposta seria "nada gravado", e valeria pelo boot inteiro.
 */
static atomic_t loaded;

/* Estado do debounce: só a thread de gravação toca. */
static int16_t seen_track = TRACK_NONE;
static int64_t seen_since;

static int handle_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	struct last_track_record rec = {0};

	ARG_UNUSED(len);
	if (strcmp(key, "last") != 0) {
		return -ENOENT;
	}
	/* Registro menor que o esperado é de outro formato: ignorado. */
	if (read_cb(cb_arg, &rec, sizeof(rec)) != (ssize_t)sizeof(rec)) {
		return -EINVAL;
	}
	rec.name[TRACK_NAME_MAX - 1U] = '\0';
	k_mutex_lock(&lt_lock, K_FOREVER);
	stored = rec;
	stored_valid = true;
	k_mutex_unlock(&lt_lock);
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(vitrolinha, "vitrolinha", NULL, handle_set, NULL, NULL);

int last_track_init(void)
{
	int err = settings_subsys_init();

	if (err == 0) {
		err = settings_load_subtree("vitrolinha");
	}
	(void)atomic_set(&loaded, 1);
	LOG_INF("Ultima faixa gravada: %s", stored_valid ? stored.name : "(nenhuma)");
	return err;
}

int last_track_restore(void)
{
	if (atomic_get(&restored) != 0) {
		return -EALREADY;
	}
	if (atomic_get(&loaded) == 0) {
		return -EAGAIN;
	}

	k_mutex_lock(&lt_lock, K_FOREVER);
	/* Fora de LIBRARY_READY a cópia volta vazia. */
	int count = library_copy(tracks, ARRAY_SIZE(tracks), NULL);
	int found = -ENOENT;

	if (count <= 0) {
		k_mutex_unlock(&lt_lock);
		return -EAGAIN;
	}
	(void)atomic_set(&restored, 1);
	for (int i = 0; stored_valid && (i < count); i++) {
		if (!tracks[i].valid || (strcmp(tracks[i].name, stored.name) != 0)) {
			continue;
		}
		/* A do índice gravado vence; senão fica a primeira com o nome. */
		if ((found < 0) || (i == stored.index)) {
			found = i;
		}
	}
	k_mutex_unlock(&lt_lock);
	return found;
}

int last_track_poll(int64_t now_ms)
{
	struct player_snapshot snap;
	int err;

	/* Sem preempção do início ao fim: entre ver o player parado e gravar,
	 * ele não pode voltar a tocar. Com as interrupções travadas pelo driver
	 * de flash, nenhuma nota pode estar agendada.
	 */
	k_sched_lock();
	snapshot_read(&snap);
	if (snap.track != seen_track) {
		seen_track = snap.track;
		seen_since = now_ms;
		err = -EAGAIN;
	} else if ((snap.track == TRACK_NONE) || (now_ms - seen_since < LAST_TRACK_DEBOUNCE_MS)) {
		err = -EAGAIN;
	} else if (snap.state == PLAYER_PLAYING) {
		err = -EBUSY;
	} else {
		k_mutex_lock(&lt_lock, K_FOREVER);
		int count = library_copy(tracks, ARRAY_SIZE(tracks), NULL);
		struct last_track_record rec = {.index = (uint8_t)snap.track};

		if (snap.track >= count) {
			err = -ENOENT;
		} else {
			memcpy(rec.name, tracks[snap.track].name, sizeof(rec.name));
			err = 0;
			if (!stored_valid || (memcmp(&rec, &stored, sizeof(rec)) != 0)) {
				err = settings_save_one(LAST_TRACK_KEY, &rec, sizeof(rec));
				/* Falhou, fica inválido e a próxima passada tenta de novo. */
				stored = rec;
				stored_valid = (err == 0);
				LOG_INF("Ultima faixa gravada: %d %s (%d)", rec.index, rec.name, err);
			}
		}
		k_mutex_unlock(&lt_lock);
	}
	k_sched_unlock();
	return err;
}

static void last_track_worker(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (;;) {
		k_msleep(LAST_TRACK_POLL_MS);
		(void)last_track_poll(k_uptime_get());
	}
}

K_THREAD_DEFINE(last_track_thread, LAST_TRACK_STACK_SIZE, last_track_worker, NULL, NULL, NULL,
		LAST_TRACK_PRIORITY, 0, SYS_FOREVER_MS);

void last_track_start(void)
{
	k_thread_start(last_track_thread);
}

void last_track_reset(void)
{
	k_mutex_lock(&lt_lock, K_FOREVER);
	memset(&stored, 0, sizeof(stored));
	stored_valid = false;
	k_mutex_unlock(&lt_lock);
	(void)atomic_set(&restored, 0);
	(void)atomic_set(&loaded, 0);
	seen_track = TRACK_NONE;
	seen_since = 0;
}

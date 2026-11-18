/**
 * @file loader.c
 * @brief Prepara somente a faixa selecionada em um vetor fixo de eventos.
 *
 * Pedidos substituem a geração corrente sem esperar E/S. Somente o worker
 * escreve no vetor, enquanto ready é falso. Resultados antigos nunca são
 * publicados. Depois da publicação, o player copia os eventos pelo índice.
 */
#include <vitrolinha/loader.h>
#include <vitrolinha/parser_csv.h>
#include <vitrolinha/storage.h>

#include <errno.h>
#include <zephyr/kernel.h>

/* A menor linha musical ocupa 4 bytes ("0,1\n"). O espaço do cabeçalho
 * cobre a última linha sem LF e o evento adicional de silêncio final.
 */
static struct playback_event events[STORAGE_FILE_MAX / 4U];
K_SEM_DEFINE(work, 0, 1);
static struct k_spinlock lock;
static uint8_t buffer[STORAGE_FILE_MAX];
static struct k_sem *consumer_wakeup;
static size_t event_count;
static size_t cursor;
static struct loader_result result;
static uint32_t generation;
static uint32_t active_id;
static int selected_track;
static bool ready;

/** @brief Gera um identificador não nulo; exige lock adquirido. */
static uint32_t new_generation(void)
{
	if (++generation == 0) {
		generation++;
	}
	active_id = generation;
	return active_id;
}

static void loader_worker(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (;;) {
		k_sem_take(&work, K_FOREVER);
		k_spinlock_key_t key = k_spin_lock(&lock);
		uint32_t id = active_id;
		int track = selected_track;
		bool pending = id != 0 && !ready;

		k_spin_unlock(&lock, key);
		if (!pending) {
			continue;
		}

		struct parser_csv_error error = {0};
		size_t count = 0;
		uint32_t total_us = 0;
		int size = storage_load(track, buffer, sizeof(buffer));
		int status = size < 0 ? size :
			parser_csv_decode(buffer, size, events, ARRAY_SIZE(events),
					  &count, &total_us, &error);

		key = k_spin_lock(&lock);
		if (id != active_id) {
			k_spin_unlock(&lock, key);
			continue;
		}
		result = (struct loader_result){
			.track = track, .status = status, .error_line = error.line,
			.total_us = total_us,
		};
		event_count = count;
		cursor = 0;
		ready = true;
		struct k_sem *wakeup = consumer_wakeup;

		k_spin_unlock(&lock, key);
		if (wakeup != NULL) {
			k_sem_give(wakeup);
		}
	}
}

K_THREAD_DEFINE(loader_thread, 4096, loader_worker, NULL, NULL, NULL, 8, 0, SYS_FOREVER_MS);

int loader_init(void)
{
	k_thread_start(loader_thread);
	return 0;
}

void loader_set_wakeup(struct k_sem *wakeup)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	consumer_wakeup = wakeup;
	bool completed = ready;

	k_spin_unlock(&lock, key);
	if (completed && wakeup != NULL) {
		k_sem_give(wakeup);
	}
}

int loader_request(int track, uint32_t *request_id)
{
	if (track < 0 || track >= LIBRARY_MAX || request_id == NULL) {
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&lock);

	selected_track = track;
	ready = false;
	*request_id = new_generation();
	k_spin_unlock(&lock, key);
	k_sem_give(&work);
	return 0;
}

int loader_get_result(uint32_t request_id, struct loader_result *out)
{
	if (request_id == 0 || out == NULL) {
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&lock);
	int err = request_id != active_id ? -ESTALE : (!ready ? -EAGAIN : 0);

	if (err == 0) {
		*out = result;
	}
	k_spin_unlock(&lock, key);
	return err;
}

int loader_next_event(uint32_t request_id, struct playback_event *out)
{
	if (request_id == 0 || out == NULL) {
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&lock);
	int status;

	if (request_id != active_id) {
		status = -ESTALE;
	} else if (!ready || result.status != 0) {
		status = -EAGAIN;
	} else if (cursor < event_count) {
		*out = events[cursor++];
		status = 1;
	} else {
		status = 0;
	}

	k_spin_unlock(&lock, key);
	return status;
}

int loader_restart(uint32_t request_id, uint32_t *new_request_id)
{
	if (request_id == 0 || new_request_id == NULL) {
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (request_id != active_id || !ready || result.status != 0) {
		k_spin_unlock(&lock, key);
		return -ESTALE;
	}
	cursor = 0;
	*new_request_id = new_generation();
	k_spin_unlock(&lock, key);
	return 0;
}

int loader_cancel(uint32_t request_id)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (request_id == 0 || request_id != active_id) {
		k_spin_unlock(&lock, key);
		return -ESTALE;
	}
	active_id = 0;
	ready = false;
	k_spin_unlock(&lock, key);
	return 0;
}

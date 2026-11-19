/**
 * @file loader.c
 * @brief Worker proprietário do parser e produtor de uma fila de 32 eventos.
 *
 * Pedidos substituem a geração corrente sem esperar E/S. Somente o worker
 * reutiliza o buffer; resultados de gerações antigas nunca são publicados.
 * O spinlock protege apenas metadados e operações de fila sem espera.
 */
#include <vitrolinha/loader.h>
#include <vitrolinha/parser.h>
#include <vitrolinha/storage.h>

#include <errno.h>
#include <zephyr/kernel.h>

/**
 * @brief Pilha do worker.
 *
 * Com folga por ora: é esta thread que atravessa o storage e o parser. O
 * número definitivo sai da medição do `CONFIG_THREAD_ANALYZER`, como o das
 * demais threads — não de estimativa.
 */
#define LOADER_STACK_SIZE 4096

/**
 * @brief Prioridade do worker: baixa, abaixo do `player`.
 *
 * O worker faz a E/S de carga e repõe a fila de eventos; quem não pode
 * esperar por ele é o `player`, que só consome a fila já pronta.
 */
#define LOADER_PRIORITY 8

K_MSGQ_DEFINE(events, sizeof(struct playback_event), 32, 4);
K_SEM_DEFINE(work, 0, 1);
/** @brief Levantado quando o resultado da geração corrente fica pronto. */
static struct k_poll_signal ready_signal = K_POLL_SIGNAL_INITIALIZER(ready_signal);
static struct k_spinlock lock;
static uint8_t buffer[STORAGE_FILE_MAX];
static struct parser parser;
static struct loader_result result;
static uint32_t generation;
static uint32_t active_id;
static int selected_track;
static bool restart;
static bool ready;
static bool finished;

/** @brief Invalida a fila antiga; chamado com lock adquirido. */
static uint32_t new_generation(void)
{
	/* Percorre 1..UINT32_MAX e volta a 1: zero é reservado para "nenhuma". */
	generation = (generation % UINT32_MAX) + 1U;
	active_id = generation;
	ready = false;
	finished = false;
	k_msgq_purge(&events);
	return active_id;
}

/** @brief Publica o resultado e acorda o player; chamado com lock adquirido. */
static void mark_ready(void)
{
	if (!ready) {
		ready = true;
		k_poll_signal_raise(&ready_signal, 0);
	}
}

static void loader_worker(void *a, void *b, void *c)
{
	uint32_t worker_id = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (;;) {
		k_sem_take(&work, K_FOREVER);
		k_spinlock_key_t key = k_spin_lock(&lock);
		uint32_t id = active_id;
		int track = selected_track;
		bool rewind = restart;

		k_spin_unlock(&lock, key);
		if (id == 0) {
			continue;
		}
		if (id != worker_id) {
			struct parser_csv_error error = {0};
			int status;

			worker_id = id;
			if (rewind) {
				status = parser_rewind(&parser);
			} else {
				int size = storage_load(track, buffer, sizeof(buffer));

				status = size < 0 ? size :
					parser_init(&parser, buffer, size, &error);
			}
			key = k_spin_lock(&lock);
			if (id != active_id) {
				k_spin_unlock(&lock, key);
				continue;
			}
			result = (struct loader_result){
				.track = track, .status = status, .error_line = error.line,
				.total_us = status == 0 ? parser_total_us(&parser) : 0,
			};
			if (status != 0) {
				mark_ready();
			}
			k_spin_unlock(&lock, key);
			if (status != 0) {
				continue;
			}
		}

		for (;;) {
			key = k_spin_lock(&lock);
			if (id != active_id || result.status != 0) {
				k_spin_unlock(&lock, key);
				break;
			}
			if (finished || k_msgq_num_free_get(&events) == 0) {
				mark_ready();
				k_spin_unlock(&lock, key);
				break;
			}
			k_spin_unlock(&lock, key);

			struct playback_event event;
			int status = parser_next(&parser, &event);

			/* Só a geração corrente escreve na fila; uma substituída sai
			 * pelo teste do topo do laço. O init validou o arquivo
			 * inteiro, então daqui só sai evento: qualquer outra resposta
			 * encerra a fila sem o silêncio final, e o player trata a
			 * fila vazia antes dele como falha.
			 */
			key = k_spin_lock(&lock);
			if (id == active_id) {
				if (status == 1) {
					(void)k_msgq_put(&events, &event, K_NO_WAIT);
				}
				finished = status != 1 || parser_finished(&parser);
			}
			k_spin_unlock(&lock, key);
		}
	}
}

K_THREAD_DEFINE(loader_thread, LOADER_STACK_SIZE, loader_worker, NULL, NULL, NULL,
		LOADER_PRIORITY, 0, SYS_FOREVER_MS);

int loader_init(void)
{
	k_thread_start(loader_thread);
	return 0;
}

int loader_request(int track, uint32_t *request_id)
{
	if (track < 0 || track >= LIBRARY_MAX || request_id == NULL) {
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&lock);

	selected_track = track;
	restart = false;
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

	/* Sob o mesmo lock de mark_ready: ou o resultado já está pronto, ou o
	 * sinal levantado depois deste reset acorda o player de novo.
	 */
	k_poll_signal_reset(&ready_signal);
	int err = request_id != active_id ? -ESTALE : (!ready ? -EAGAIN : 0);

	if (err == 0) {
		*out = result;
	}
	k_spin_unlock(&lock, key);
	return err;
}

void loader_poll_event_init(struct k_poll_event *event)
{
	k_poll_event_init(event, K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &ready_signal);
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
	} else if (k_msgq_get(&events, out, K_NO_WAIT) == 0) {
		status = 1;
	} else {
		status = finished ? 0 : -EAGAIN;
	}
	bool refill = status == 1 && !finished;

	k_spin_unlock(&lock, key);
	if (refill) {
		k_sem_give(&work);
	}
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
	restart = true;
	*new_request_id = new_generation();
	k_spin_unlock(&lock, key);
	k_sem_give(&work);
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
	k_msgq_purge(&events);
	k_spin_unlock(&lock, key);
	return 0;
}

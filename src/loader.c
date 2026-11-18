/**
 * @file loader.c
 * @brief Worker proprietário do CSV/parser e produtor de uma fila de 32
 * eventos.
 *
 * Pedidos substituem a geração corrente sem esperar E/S. Somente o worker
 * reutiliza o buffer; resultados de gerações antigas nunca são publicados.
 * O spinlock protege apenas metadados e operações de fila sem espera.
 */
#include <vitrolinha/loader.h>
#include <vitrolinha/parser_csv.h>
#include <vitrolinha/storage.h>

#include <errno.h>
#include <zephyr/kernel.h>

K_MSGQ_DEFINE(events, sizeof(struct playback_event), 32, 4);
K_SEM_DEFINE(work, 0, 1);
static struct k_spinlock lock;
static uint8_t buffer[STORAGE_FILE_MAX];
static struct parser_csv parser;
static struct loader_result result;
static uint32_t generation;
static uint32_t active_id;
static int selected_track;
static bool restart;
static bool ready;
static bool finished;

/** @brief Invalida a fila antiga; chamado com lock adquirido. */
static uint32_t new_generation(void) {
    if (++generation == 0) {
        generation++;
    }
    active_id = generation;
    ready = false;
    finished = false;
    k_msgq_purge(&events);
    return active_id;
}

static void loader_worker(void *a, void *b, void *c) {
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
                status = parser_csv_rewind(&parser);
            } else {
                /* int size = storage_load(track, buffer, sizeof(buffer)); */
                int size = 3;

                status = size < 0
                             ? size
                             : parser_csv_init(&parser, buffer, size, &error);
            }
            key = k_spin_lock(&lock);
            if (id != active_id) {
                k_spin_unlock(&lock, key);
                continue;
            }
            result = (struct loader_result){
                .track = track,
                .status = status,
                .error_line = error.line,
                .total_us = status == 0 ? parser.total_us : 0,
            };
            ready = status != 0;
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
                ready = true;
                k_spin_unlock(&lock, key);
                break;
            }
            k_spin_unlock(&lock, key);

            struct playback_event event;
            int status = parser_csv_next(&parser, &event);

            key = k_spin_lock(&lock);
            if (id != active_id) {
                k_spin_unlock(&lock, key);
                break;
            }
            if (status == 1) {
                (void)k_msgq_put(&events, &event, K_NO_WAIT);
                finished = parser.final_event_emitted;
            } else {
                result.status = status < 0 ? status : -EIO;
                ready = true;
            }
            k_spin_unlock(&lock, key);
        }
    }
}

K_THREAD_DEFINE(loader_thread, 4096, loader_worker, NULL, NULL, NULL, 8, 0,
                SYS_FOREVER_MS);

int loader_init(void) {
    k_thread_start(loader_thread);
    return 0;
}

int loader_request(int track, uint32_t *request_id) {
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

int loader_get_result(uint32_t request_id, struct loader_result *out) {
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

int loader_next_event(uint32_t request_id, struct playback_event *out) {
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

int loader_restart(uint32_t request_id, uint32_t *new_request_id) {
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

int loader_cancel(uint32_t request_id) {
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

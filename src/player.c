/**
 * @file player.c
 * @brief Transporte por comandos e aplicação de eventos em prazos absolutos.
 *
 * O lock serializa a thread e o timer. Nenhum deles lê arquivos ou interpreta
 * CSV. A thread dorme até receber um comando ou uma conclusão de carga.
 */
#include <vitrolinha/player.h>
#include <vitrolinha/cmd.h>
#include <vitrolinha/loader.h>
#include <vitrolinha/snapshot.h>
#include <vitrolinha/tone.h>

#include <errno.h>
#include <zephyr/kernel.h>

static struct k_spinlock lock;
static struct k_timer timer;
static struct player_snapshot state = SNAPSHOT_IDLE_INITIALIZER;
static struct playback_event current;
static struct playback_event next;
static uint32_t request_id;
static size_t library_count;
static bool loading;
K_SEM_DEFINE(player_wakeup, 0, 1);

static uint64_t now_us(void)
{
	return k_ticks_to_us_floor64(k_uptime_ticks());
}

/** @brief Silencia e publica parada; todos os helpers exigem lock adquirido. */
static void stop_at(uint32_t elapsed)
{
	k_timer_stop(&timer);
	(void)tone_set_frequency(0);
	state.state = PLAYER_STOPPED;
	state.base_us = elapsed;
	snapshot_publish(&state);
}

static void arm_next(void)
{
	k_timer_start(&timer,
		K_TIMEOUT_ABS_TICKS(k_us_to_ticks_ceil64(state.base_us + next.offset_us)),
		K_NO_WAIT);
}

/** @brief Aplica o evento corrente e agenda somente o próximo. */
static void apply_current(void)
{
	if (tone_set_frequency(current.freq_hz) != 0) {
		stop_at(current.offset_us);
		return;
	}
	int status = loader_next_event(request_id, &next);

	if (status == 0 && current.offset_us == state.total_us && current.freq_hz == 0) {
		state.state = PLAYER_STOPPED;
		state.base_us = state.total_us;
		snapshot_publish(&state);
	} else if (status != 1) {
		/* Evento indisponível antes do silêncio final é falha. */
		stop_at(current.offset_us);
	} else {
		arm_next();
	}
}

static void timer_expired(struct k_timer *expired)
{
	ARG_UNUSED(expired);
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (state.state == PLAYER_PLAYING) {
		current = next;
		apply_current();
	}
	k_spin_unlock(&lock, key);
}

static void select_track(int track)
{
	stop_at(0);
	int err = loader_request(track, &request_id);

	loading = err == 0;
	state.track = track;
	state.total_us = 0;
	snapshot_publish(&state);
}

static void handle_command(const struct cmd *command)
{
	switch (command->kind) {
	case CMD_SELECT:
		if ((size_t)command->track < library_count) {
			select_track(command->track);
		}
		break;
	case CMD_NEXT:
	case CMD_PREV:
		if (library_count > 0) {
			int track = state.track;

			if (track == TRACK_NONE) {
				track = command->kind == CMD_NEXT ? 0 : library_count - 1;
			} else if (command->kind == CMD_NEXT) {
				track = (track + 1) % library_count;
			} else {
				track = (track + library_count - 1) % library_count;
			}
			select_track(track);
		}
		break;
	case CMD_STOP:
		if (loading) {
			(void)loader_cancel(request_id);
			request_id = 0;
			loading = false;
		}
		stop_at(0);
		break;
	case CMD_PLAY_PAUSE:
		if (loading) {
			break;
		}
		if (state.state == PLAYER_PLAYING) {
			uint32_t elapsed = snapshot_elapsed_us(&state, now_us());

			k_timer_stop(&timer);
			int err = tone_set_frequency(0);

			state.state = err == 0 ? PLAYER_PAUSED : PLAYER_STOPPED;
			state.base_us = elapsed;
			snapshot_publish(&state);
		} else if (state.state == PLAYER_PAUSED) {
			/* Retoma do início da nota, preservando sua duração integral. */
			state.base_us = now_us() - current.offset_us;
			state.state = PLAYER_PLAYING;
			if (tone_set_frequency(current.freq_hz) != 0) {
				stop_at(current.offset_us);
			} else {
				arm_next();
				snapshot_publish(&state);
			}
		} else if (request_id != 0 &&
			   loader_restart(request_id, &request_id) == 0) {
			loading = true;
			state.base_us = 0;
			snapshot_publish(&state);
		} else if (library_count > 0) {
			select_track(state.track == TRACK_NONE ? 0 : state.track);
		}
		break;
	default:
		break;
	}
}

static void check_load(void)
{
	struct loader_result result;
	int err = loader_get_result(request_id, &result);

	if (err == -EAGAIN) {
		return;
	}
	loading = false;
	if (err != 0 || result.status != 0) {
		(void)loader_cancel(request_id);
		request_id = 0;
		stop_at(0);
		return;
	}
	state.total_us = result.total_us;
	if (loader_next_event(request_id, &current) != 1) {
		stop_at(0);
		return;
	}
	state.state = PLAYER_PLAYING;
	state.base_us = now_us();
	snapshot_publish(&state);
	apply_current();
}

static void player_worker(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (;;) {
		struct cmd command;

		k_sem_take(&player_wakeup, K_FOREVER);
		while (cmd_get(&command, K_NO_WAIT) == 0) {
			k_spinlock_key_t key = k_spin_lock(&lock);

			handle_command(&command);
			k_spin_unlock(&lock, key);
		}
		k_spinlock_key_t key = k_spin_lock(&lock);

		if (loading) {
			check_load();
		}
		k_spin_unlock(&lock, key);
	}
}

K_THREAD_DEFINE(player_thread, 2048, player_worker, NULL, NULL, NULL, 4, 0, SYS_FOREVER_MS);

int player_init(size_t track_count)
{
	if (track_count > LIBRARY_MAX) {
		return -EINVAL;
	}
	library_count = track_count;
	k_timer_init(&timer, timer_expired, NULL);
	snapshot_publish(&state);
	cmd_set_wakeup(&player_wakeup);
	loader_set_wakeup(&player_wakeup);
	k_thread_start(player_thread);
	return 0;
}

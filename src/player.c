/**
 * @file player.c
 * @brief Transporte por comandos e aplicação de eventos em prazos absolutos.
 *
 * O lock serializa a thread e o timer. Nenhum deles lê arquivos ou interpreta
 * a faixa. A thread só acorda por evento: comando na fila ou, durante a carga,
 * resultado pronto no loader.
 *
 * A biblioteca chega só por leituras atômicas de library.h: o player nunca
 * espera a varredura nem a trava da lista (ADR 0010).
 *
 * Avanço automático (RF09, `CONFIG_VITROLINHA_AUTO_ADVANCE`): o fim da faixa
 * é visto no callback do timer, em interrupção, onde não se carrega arquivo.
 * O callback só marca @ref ended e levanta @ref end_signal; a thread acorda
 * e pede a vizinha seguinte como faria um ::CMD_NEXT — mas sem dar a volta:
 * depois da última faixa válida, ou com uma só, o player para.
 */
#include <vitrolinha/player.h>
#include <vitrolinha/cmd.h>
#include <vitrolinha/library.h>
#include <vitrolinha/loader.h>
#include <vitrolinha/snapshot.h>
#include <vitrolinha/storage.h>
#include <vitrolinha/tone.h>

#include "priorities.h"

#include <errno.h>
#include <zephyr/kernel.h>

/* Um arquivo de STORAGE_FILE_MAX bytes não tem mais linhas do que bytes, então
 * a linha do erro cabe no uint16_t do instantâneo sem saturar.
 */
BUILD_ASSERT(STORAGE_FILE_MAX <= UINT16_MAX,
	     "error_line e error_column do instantâneo são de 16 bits");

/**
 * @brief Pilha da thread `player`.
 *
 * A thread só mexe em estado em RAM, sem E/S nem gráficos. O número
 * definitivo sai da medição do `CONFIG_THREAD_ANALYZER` — não de estimativa.
 */
#define PLAYER_STACK_SIZE 2048

/*
 * Prioridade: ::PRIORITY_PLAYER, a mais alta do projeto, acima do worker do
 * loader e da fila de trabalho do LVGL, para que nenhuma carga ou redesenho
 * atrase um comando de transporte. A ordem inteira está em src/priorities.h.
 */

static struct k_spinlock lock;
static struct k_timer timer;
static struct player_snapshot state = SNAPSHOT_IDLE_INITIALIZER;
static struct playback_event current;
static struct playback_event next;
static uint32_t request_id;
static bool loading;

/** @brief Fim de faixa visto pelo callback, à espera da thread. Sob @ref lock. */
static bool ended;

/** @brief Acorda a thread no fim da faixa; levantado em interrupção. */
static struct k_poll_signal end_signal = K_POLL_SIGNAL_INITIALIZER(end_signal);

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
		if (IS_ENABLED(CONFIG_VITROLINHA_AUTO_ADVANCE)) {
			/* Só sinaliza: carregar a próxima é da thread. */
			ended = true;
			k_poll_signal_raise(&end_signal, 0);
		}
	} else if (status != 1) {
		/* Fila vazia antes do silêncio final é falha, não fim de faixa. */
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
	/* Antes do stop_at, para a parada já sair publicada sem o erro antigo. */
	state.load_error = 0;
	state.error_line = 0;
	state.error_column = 0;
	stop_at(0);
	int err = loader_request(track, &request_id);

	loading = err == 0;
	state.track = track;
	state.total_us = 0;
	snapshot_publish(&state);
}

/**
 * @brief Avanço automático: a faixa seguinte, se houver uma adiante.
 *
 * O vizinho de library_neighbor dá a volta nas pontas, e é por isso que só
 * vale um índice maior que o atual: a volta, inclusive a da faixa única que
 * é vizinha de si mesma, quer dizer que a lista acabou. ::TRACK_NONE —
 * cartão fora, nenhuma válida — também não é maior, e o player fica parado
 * em 100%.
 */
static void advance(void)
{
	int track = library_neighbor(state.track, true);

	if (track > state.track) {
		select_track(track);
	}
}

static void handle_command(const struct cmd *command)
{
	/* O comando do usuário vence o avanço automático da mesma volta. */
	ended = false;
	/* Sem default: cmd_post() recusa tipos desconhecidos na entrada. */
	switch (command->kind) {
	case CMD_SELECT:
		/* Índice além da lista chega ao loader e volta como -ENOENT. */
		if (library_state() == LIBRARY_READY) {
			select_track(command->track);
		}
		break;
	case CMD_NEXT:
	case CMD_PREV: {
		/* TRACK_NONE fora de ::LIBRARY_READY ou sem faixa tocável: é
		 * isto que desliga seguinte e anterior com o cartão fora.
		 */
		int track = library_neighbor(state.track, command->kind == CMD_NEXT);

		if (track != TRACK_NONE) {
			select_track(track);
		}
		break;
	}
	case CMD_CUE:
		/* Só sem faixa nenhuma, isto é, no boot: nunca troca a escolha
		 * do usuário. Sem faixa também não há carga nem pedido retido,
		 * e tocar/pausar passa a carregar esta.
		 */
		if (state.track == TRACK_NONE) {
			state.track = command->track;
			snapshot_publish(&state);
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
		} else if (library_state() == LIBRARY_READY) {
			/* Sem faixa ainda, a primeira tocável; se não há nenhuma,
			 * o vizinho é TRACK_NONE e o comando não faz nada.
			 */
			int track = state.track != TRACK_NONE ? state.track
							      : library_neighbor(TRACK_NONE, true);

			if (track != TRACK_NONE) {
				select_track(track);
			}
		}
		break;
	}
}

static void check_load(void)
{
	/* Zerado: loader_get_result só preenche quando devolve 0. */
	struct loader_result result = {0};
	int err = loader_get_result(request_id, &result);

	if (err == -EAGAIN) {
		return;
	}
	loading = false;
	/* Resultado pronto e sem erro sempre traz ao menos um evento na fila;
	 * a terceira condição é defesa, e falha igual a uma carga recusada.
	 */
	if (err != 0 || result.status != 0 ||
	    loader_next_event(request_id, &current) != 1) {
		int failure = err != 0 ? err : result.status;

		/* A terceira condição é defesa e chega aqui com failure zero; uma
		 * carga que falhou nunca pode ser publicada como erro 0.
		 */
		state.load_error = (int16_t)(failure != 0 ? failure : -EIO);
		state.error_line = (uint16_t)result.error_line;
		state.error_column = (uint16_t)result.error_column;
		(void)loader_cancel(request_id);
		request_id = 0;
		stop_at(0);
		return;
	}
	state.total_us = result.total_us;
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
	struct k_poll_event events[3];

	cmd_poll_event_init(&events[0]);
	k_poll_event_init(&events[1], K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &end_signal);
	loader_poll_event_init(&events[2]);
	for (;;) {
		struct cmd command;

		events[0].state = K_POLL_STATE_NOT_READY;
		events[1].state = K_POLL_STATE_NOT_READY;
		events[2].state = K_POLL_STATE_NOT_READY;
		/* O resultado do loader só interessa, e só acorda, durante a carga. */
		(void)k_poll(events, loading ? 3 : 2, K_FOREVER);
		int err = cmd_get(&command, K_NO_WAIT);
		k_spinlock_key_t key = k_spin_lock(&lock);

		/* Sob o lock do callback: ou o fim já está em @ref ended, ou o
		 * sinal levantado depois deste reset acorda a thread de novo.
		 */
		k_poll_signal_reset(&end_signal);
		if (err == 0) {
			handle_command(&command);
		}
		if (ended) {
			ended = false;
			advance();
		}
		if (loading) {
			check_load();
		}
		k_spin_unlock(&lock, key);
	}
}

K_THREAD_DEFINE(player_thread, PLAYER_STACK_SIZE, player_worker, NULL, NULL, NULL,
		PRIORITY_PLAYER, 0, SYS_FOREVER_MS);

int player_init(void)
{
	k_timer_init(&timer, timer_expired, NULL);
	snapshot_publish(&state);
	k_thread_start(player_thread);
	return 0;
}

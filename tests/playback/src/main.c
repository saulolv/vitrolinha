/** @file main.c
 * @brief Testes do fluxo CSV -> loader -> player -> captura no host.
 */
#define _POSIX_C_SOURCE 200809L
#include <vitrolinha/cmd.h>
#include <vitrolinha/loader.h>
#include <vitrolinha/playback_trace.h>
#include <vitrolinha/player.h>
#include <vitrolinha/snapshot.h>
#include <vitrolinha/storage.h>
#include <vitrolinha/tone.h>

#include "host_libc_fault.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zephyr/ztest.h>

static const uint8_t escala[] = {
#include "escala.inc"
};
static const uint8_t pausas[] = {
#include "pausas.inc"
};
static const uint8_t repetidas[] = {
#include "notas_repetidas.inc"
};
static char directory[] = "/tmp/vitrolinha-playback-XXXXXX";
static char path[128];

static uint64_t clock_us(void)
{
	return k_cyc_to_us_floor64(k_cycle_get_64());
}

static void file_path(const char *name)
{
	snprintf(path, sizeof(path), "%s/%s", directory, name);
}

static void write_file(const char *name, const void *data, size_t length)
{
	file_path(name);
	FILE *file = fopen(path, "wb");

	zassert_not_null(file);
	zassert_equal(length, fwrite(data, 1, length, file));
	zassert_ok(fclose(file));
}

static void *setup(void)
{
	struct track_meta tracks[LIBRARY_MAX];
	const char invalid[] = "frequency_hz,duration_ms\n1000,0\n";
	const char out_of_range[] = "frequency_hz,duration_ms\n440,100\n19,100\n";
	const char rtttl[] = "Teste:d=4,o=5,b=120:c,e";

	zassert_not_null(mkdtemp(directory));
	write_file("0.csv", escala, sizeof(escala));
	write_file("1.csv", pausas, sizeof(pausas));
	write_file("2.csv", repetidas, sizeof(repetidas));
	write_file("3.csv", invalid, sizeof(invalid) - 1);
	file_path("4.csv");
	FILE *file = fopen(path, "w");

	zassert_not_null(file);
	fputs("frequency_hz,duration_ms\n", file);
	for (int i = 0; i < 64; i++) {
		fputs("1000,10\n", file);
	}
	zassert_ok(fclose(file));
	write_file("5.csv", out_of_range, sizeof(out_of_range) - 1);
	write_file("6.rtx", rtttl, sizeof(rtttl) - 1);
	zassert_ok(setenv("VITROLINHA_MUSIC_DIR", directory, 1));
	zassert_equal(7, storage_scan(tracks, ARRAY_SIZE(tracks), NULL));
	zassert_str_equal("Teste", tracks[6].name, "o nome sai do cabecalho RTTTL");
	zassert_ok(loader_init());
	zassert_ok(tone_init());
	return NULL;
}

static void teardown(void *fixture)
{
	ARG_UNUSED(fixture);
	for (int i = 0; i < 6; i++) {
		char name[8];

		snprintf(name, sizeof(name), "%d.csv", i);
		file_path(name);
		/* O teste de reinício remove 4.csv antes de pedir a faixa em RAM. */
		zassert_true(unlink(path) == 0 || errno == ENOENT);
	}
	file_path("6.rtx");
	zassert_ok(unlink(path));
	file_path("trace.csv");
	zassert_ok(unlink(path));
	zassert_ok(rmdir(directory));
	zassert_ok(unsetenv("VITROLINHA_MUSIC_DIR"));
}

static struct loader_result await_load(uint32_t id)
{
	struct loader_result result;
	int status = -EAGAIN;

	for (int i = 0; i < 100 && status == -EAGAIN; i++) {
		status = loader_get_result(id, &result);
		if (status == -EAGAIN) {
			k_msleep(1);
		}
	}
	zassert_ok(status);
	return result;
}

ZTEST(playback, test_loader_substituicao_fila_e_reinicio)
{
	uint32_t old_id, id;
	struct playback_event event;
	struct loader_result result;

	zassert_ok(loader_request(3, &old_id));
	zassert_ok(loader_request(4, &id));
	zassert_equal(-ESTALE, loader_get_result(old_id, &result));
	result = await_load(id);
	zassert_ok(result.status);
	zassert_equal(640000, result.total_us);
	for (int i = 0; i <= 64; i++) {
		zassert_equal(1, loader_next_event(id, &event));
		zassert_equal(i * 10000, event.offset_us);
		zassert_equal(i == 64 ? 0 : 1000, event.freq_hz);
		k_msleep(1); /* Permite reposição pelo worker, inclusive após 32 eventos. */
	}
	zassert_equal(0, loader_next_event(id, &event));
	file_path("4.csv");
	zassert_ok(unlink(path));
	old_id = id;
	zassert_ok(loader_restart(old_id, &id));
	zassert_equal(-ESTALE, loader_next_event(old_id, &event));
	result = await_load(id);
	zassert_ok(result.status);
	zassert_equal(1, loader_next_event(id, &event));
	zassert_equal(0, event.offset_us);
	zassert_equal(1000, event.freq_hz);
	zassert_ok(loader_cancel(id));
	zassert_equal(-ESTALE, loader_next_event(id, &event));
	zassert_ok(loader_request(3, &id));
	result = await_load(id);
	zassert_equal(-EINVAL, result.status);
	zassert_equal(2, result.error_line);
	zassert_ok(loader_cancel(id));
	/* Frequência que o buzzer recusaria: falha na carga, com a linha, e
	 * não no meio da reprodução.
	 */
	zassert_ok(loader_request(5, &id));
	result = await_load(id);
	zassert_equal(-ERANGE, result.status);
	zassert_equal(3, result.error_line);
	zassert_ok(loader_cancel(id));
	/* RTTTL pelo mesmo loader: o formato sai do conteúdo. Duas semínimas a
	 * 120 bpm, Dó 5 e Mi 5, cada uma com o silêncio de articulação.
	 */
	zassert_ok(loader_request(6, &id));
	result = await_load(id);
	zassert_ok(result.status);
	zassert_equal(1000000U, result.total_us);
	zassert_equal(1, loader_next_event(id, &event));
	zassert_equal(0, event.offset_us);
	zassert_equal(523, event.freq_hz);
	zassert_equal(1, loader_next_event(id, &event));
	zassert_equal(500000U - 20000U, event.offset_us);
	zassert_equal(0, event.freq_hz);
	zassert_equal(1, loader_next_event(id, &event));
	zassert_equal(500000U, event.offset_us);
	zassert_equal(659, event.freq_hz);
	zassert_ok(loader_cancel(id));
}

ZTEST(playback, test_trace_timestamps_exportacao_e_limite)
{
	struct playback_trace_entry entry;
	uint64_t start = clock_us();

	zassert_ok(playback_trace_begin(start));
	zassert_equal(-EBUSY, playback_trace_begin(start));
	zassert_ok(tone_set_frequency(1000));
	k_msleep(10);
	uint64_t second = clock_us();

	zassert_ok(tone_set_frequency(1000));
	zassert_ok(tone_set_frequency(0));
	/* A faixa da placa vale no simulador, e a recusa não entra na captura:
	 * a leitura do índice 4, abaixo, confirma que nada foi registrado.
	 */
	zassert_equal(-ERANGE, tone_set_frequency(TONE_FREQ_MIN_HZ - 1U));
	zassert_equal(-ERANGE, tone_set_frequency(TONE_FREQ_MAX_HZ + 1U));
	zassert_equal(-EINVAL, playback_trace_record(start, 1000));
	zassert_equal(-EINVAL, playback_trace_end(start));
	zassert_equal(-EBUSY, playback_trace_read(0, &entry));
	zassert_ok(playback_trace_end(clock_us()));
	zassert_equal(1, playback_trace_read(1, &entry));
	zassert_equal(second - start, entry.elapsed_us);
	zassert_equal(1000, entry.freq_hz);
	zassert_equal(1, playback_trace_read(3, &entry));
	zassert_equal(0, entry.freq_hz);
	zassert_equal(0, playback_trace_read(4, &entry));
	file_path("trace.csv");
	zassert_ok(playback_trace_export_csv(path));
	FILE *file = fopen(path, "r");
	char text[256];

	zassert_not_null(file);
	size_t length = fread(text, 1, sizeof(text) - 1, file);
	text[length] = '\0';
	zassert_ok(fclose(file));
	zassert_true(strncmp(text, "elapsed_us,frequency_hz\n", 24) == 0);
	zassert_not_null(strstr(text, ",1000\n"));
	zassert_equal(-EACCES, tone_set_frequency(1000));

	zassert_ok(playback_trace_begin(0));
	for (size_t i = 0; i < PLAYBACK_TRACE_CAPACITY; i++) {
		zassert_ok(playback_trace_record(i, 1000));
	}
	zassert_equal(-ENOSPC, playback_trace_record(PLAYBACK_TRACE_CAPACITY, 0));
	zassert_equal(-ENOSPC, playback_trace_end(PLAYBACK_TRACE_CAPACITY));
	zassert_equal(-ENOSPC, playback_trace_read(0, &entry));
	zassert_equal(-ENOSPC, playback_trace_export_csv(path));
}

ZTEST(playback, test_trace_argumentos_e_falhas_de_exportacao)
{
	zassert_equal(-EINVAL, playback_trace_read(0, NULL));
	zassert_equal(-EINVAL, playback_trace_export_csv(NULL));
	zassert_equal(-EINVAL, playback_trace_export_csv(""));

	zassert_ok(playback_trace_begin(100));
	zassert_equal(-EINVAL, playback_trace_record(50, 1000));
	zassert_equal(-EBUSY, playback_trace_export_csv("/dev/full"));
	zassert_ok(playback_trace_record(100, 1000));
	zassert_ok(playback_trace_end(200));

	file_path("ausente/trace.csv");
	zassert_equal(-EIO, playback_trace_export_csv(path));
	/* Poucas linhas cabem no buffer do stream: o erro só aparece no fclose. */
	zassert_equal(-EIO, playback_trace_export_csv("/dev/full"));

	/* Muitas linhas transbordam o buffer: o erro aparece já nas escritas. */
	zassert_ok(playback_trace_begin(0));
	for (size_t i = 0; i < PLAYBACK_TRACE_CAPACITY - 1U; i++) {
		zassert_ok(playback_trace_record(i, 1000));
	}
	zassert_ok(playback_trace_end(PLAYBACK_TRACE_CAPACITY));
	zassert_equal(-EIO, playback_trace_export_csv("/dev/full"));
}

ZTEST(playback, test_loader_argumentos_e_pedidos_fora_de_hora)
{
	uint32_t id, other;
	struct loader_result result;
	struct playback_event event;

	zassert_equal(-EINVAL, loader_request(-1, &id));
	zassert_equal(-EINVAL, loader_request(LIBRARY_MAX, &id));
	zassert_equal(-EINVAL, loader_request(0, NULL));
	zassert_equal(-EINVAL, loader_get_result(0, &result));
	zassert_equal(-EINVAL, loader_next_event(0, &event));
	zassert_equal(-EINVAL, loader_restart(0, &other));
	zassert_equal(-ESTALE, loader_cancel(0));

	/* O teste é cooperativo: até ele dormir, o worker não roda. */
	zassert_ok(loader_request(4, &id));
	zassert_equal(-EINVAL, loader_get_result(id, NULL));
	zassert_equal(-EINVAL, loader_next_event(id, NULL));
	zassert_equal(-EINVAL, loader_restart(id, NULL));
	zassert_equal(-EAGAIN, loader_get_result(id, &result));
	zassert_equal(-EAGAIN, loader_next_event(id, &event));
	zassert_equal(-ESTALE, loader_restart(id, &other));
	/* Cancelado antes de o worker acordar: ele acorda sem pedido e volta. */
	zassert_ok(loader_cancel(id));
	zassert_equal(-ESTALE, loader_cancel(id));
	zassert_equal(-ESTALE, loader_restart(id, &other));
	k_msleep(5);

	/* Consumo mais rápido que a reposição: fila vazia não é fim de faixa. */
	zassert_ok(loader_request(4, &id));
	result = await_load(id);
	zassert_ok(result.status);
	for (int i = 0; i < 32; i++) {
		zassert_equal(1, loader_next_event(id, &event));
	}
	zassert_equal(-EAGAIN, loader_next_event(id, &event));
	k_msleep(1);
	zassert_equal(1, loader_next_event(id, &event));
	zassert_equal(32 * 10000, event.offset_us);
	zassert_ok(loader_cancel(id));

	/* Faixa que o storage não entrega: o erro dele é o resultado. */
	zassert_ok(loader_request(10, &id));
	result = await_load(id);
	zassert_equal(-ENOENT, result.status);
	zassert_equal(-EAGAIN, loader_next_event(id, &event));
	zassert_equal(-ESTALE, loader_restart(id, &other));
	zassert_ok(loader_cancel(id));
}

static uint32_t hooked_id;
static int hooked_err;

/** @brief Pedido novo no meio da leitura do arquivo pelo worker. */
static void replace_request(void)
{
	hooked_err = loader_request(2, &hooked_id);
}

ZTEST(playback, test_loader_pedido_substituido_durante_a_leitura)
{
	uint32_t id;
	struct loader_result result;

	hooked_id = 0;
	hooked_err = -1;
	host_libc_fault_on_read(replace_request);
	zassert_ok(loader_request(0, &id));
	k_msleep(5);
	zassert_ok(hooked_err);
	/* A carga que terminou depois de substituída não é publicada. */
	zassert_equal(-ESTALE, loader_get_result(id, &result));
	result = await_load(hooked_id);
	zassert_ok(result.status);
	zassert_equal(2, result.track);
	zassert_ok(loader_cancel(hooked_id));
}

static struct player_snapshot await_state(enum player_state wanted, int track)
{
	struct player_snapshot snap;

	for (int i = 0; i < 3100; i++) {
		snapshot_read(&snap);
		if (snap.state == wanted && snap.track == track) {
			return snap;
		}
		k_msleep(1);
	}
	zassert_true(false, "estado %d, faixa %d; obtido %u, %d",
		     wanted, track, snap.state, snap.track);
	return snap;
}

/** @brief O player é iniciado uma vez por processo, por quem chegar antes. */
static void start_player(void)
{
	static bool started;

	cmd_flush();
	if (!started) {
		zassert_equal(-EINVAL, player_init(LIBRARY_MAX + 1U));
		zassert_ok(player_init(5));
		started = true;
	}
}

/** @brief Toma o loader do player, como se outro pedido o substituísse. */
static void steal_loader(void)
{
	uint32_t id;

	if (loader_request(1, &id) == 0) {
		(void)loader_cancel(id);
	}
}

static struct player_snapshot settle(void)
{
	struct player_snapshot snap;

	k_msleep(5);
	snapshot_read(&snap);
	return snap;
}

ZTEST(playback, test_player_falhas_e_comandos_fora_de_hora)
{
	struct player_snapshot before, snap;

	start_player();
	(void)playback_trace_end(clock_us());

	/* Faixa além da biblioteca: ignorada. */
	snapshot_read(&before);
	zassert_ok(cmd_post(CMD_SELECT, 6));
	snap = settle();
	zassert_equal(before.state, snap.state);
	zassert_equal(before.track, snap.track);

	/* Arquivo inválido: para na faixa, e tocar de novo tenta carregá-la. */
	zassert_ok(cmd_post(CMD_SELECT, 3));
	snap = settle();
	zassert_equal(PLAYER_STOPPED, snap.state);
	zassert_equal(3, snap.track);
	/* O motivo e a linha chegam à tela: duração zero na linha 2. */
	zassert_equal(-EINVAL, snap.load_error);
	zassert_equal(2, snap.error_line);
	zassert_ok(cmd_post(CMD_PLAY_PAUSE, TRACK_NONE));
	snap = settle();
	zassert_equal(PLAYER_STOPPED, snap.state);
	zassert_equal(3, snap.track);
	zassert_equal(-EINVAL, snap.load_error);
	zassert_equal(2, snap.error_line);

	/* Parar durante a carga cancela o pedido: nada chega a tocar. */
	zassert_ok(playback_trace_begin(clock_us()));
	zassert_ok(cmd_post(CMD_SELECT, 0));
	zassert_ok(cmd_post(CMD_STOP, TRACK_NONE));
	k_msleep(300);
	snapshot_read(&snap);
	zassert_equal(PLAYER_STOPPED, snap.state);
	zassert_equal(0, snap.track);
	/* Selecionar outra faixa apaga o erro da anterior. */
	zassert_equal(0, snap.load_error);
	zassert_equal(0, snap.error_line);

	/* Tocar/pausar durante a carga é ignorado. */
	zassert_ok(cmd_post(CMD_SELECT, 0));
	zassert_ok(cmd_post(CMD_PLAY_PAUSE, TRACK_NONE));
	snap = settle();
	zassert_equal(PLAYER_PLAYING, snap.state);
	zassert_ok(cmd_post(CMD_STOP, TRACK_NONE));
	(void)await_state(PLAYER_STOPPED, 0);
	zassert_ok(playback_trace_end(clock_us()));

	/* Saída recusando o primeiro tom: para no início. */
	zassert_ok(cmd_post(CMD_SELECT, 1));
	snap = settle();
	zassert_equal(PLAYER_STOPPED, snap.state);
	zassert_equal(1, snap.track);
	zassert_equal(0, snap.base_us);
	/* Falha da saída não é defeito do arquivo. */
	zassert_equal(0, snap.load_error);

	/* Saída recusando o silêncio da pausa: para em vez de pausar. */
	zassert_ok(playback_trace_begin(clock_us()));
	zassert_ok(cmd_post(CMD_SELECT, 0));
	(void)await_state(PLAYER_PLAYING, 0);
	zassert_ok(playback_trace_end(clock_us()));
	zassert_ok(cmd_post(CMD_PLAY_PAUSE, TRACK_NONE));
	snap = settle();
	zassert_equal(PLAYER_STOPPED, snap.state);

	/* Saída recusando o tom da retomada: para em vez de retomar. */
	zassert_ok(playback_trace_begin(clock_us()));
	zassert_ok(cmd_post(CMD_SELECT, 0));
	(void)await_state(PLAYER_PLAYING, 0);
	zassert_ok(cmd_post(CMD_PLAY_PAUSE, TRACK_NONE));
	(void)await_state(PLAYER_PAUSED, 0);
	zassert_ok(playback_trace_end(clock_us()));
	zassert_ok(cmd_post(CMD_PLAY_PAUSE, TRACK_NONE));
	snap = settle();
	zassert_equal(PLAYER_STOPPED, snap.state);

	/* Fila que deixa de entregar antes do silêncio final: falha, não fim. */
	zassert_ok(playback_trace_begin(clock_us()));
	zassert_ok(cmd_post(CMD_SELECT, 0));
	(void)await_state(PLAYER_PLAYING, 0);
	steal_loader();
	snap = await_state(PLAYER_STOPPED, 0);
	zassert_equal(250000, snap.base_us);
	zassert_true(snap.base_us < snap.total_us);

	/* Sem a faixa retida no loader, tocar carrega a mesma faixa de novo. */
	zassert_ok(cmd_post(CMD_PLAY_PAUSE, TRACK_NONE));
	(void)await_state(PLAYER_PLAYING, 0);
	zassert_ok(cmd_post(CMD_STOP, TRACK_NONE));
	(void)await_state(PLAYER_STOPPED, 0);

	/* Pedido substituído durante a carga: o player para quando acorda. */
	host_libc_fault_on_read(steal_loader);
	zassert_ok(cmd_post(CMD_SELECT, 2));
	k_msleep(5);
	zassert_ok(cmd_post(CMD_PLAY_PAUSE, TRACK_NONE));
	snap = settle();
	zassert_equal(PLAYER_STOPPED, snap.state);
	zassert_equal(2, snap.track);
	zassert_equal(-ESTALE, snap.load_error);
	zassert_equal(0, snap.error_line);
	zassert_ok(cmd_post(CMD_SELECT, 1));
	snap = await_state(PLAYER_PLAYING, 1);
	zassert_equal(0, snap.load_error);
	zassert_ok(cmd_post(CMD_STOP, TRACK_NONE));
	(void)await_state(PLAYER_STOPPED, 1);
	zassert_ok(playback_trace_end(clock_us()));
}

ZTEST(playback, test_player_csvs_e_transporte)
{
	const uint32_t frequencies[][9] = {
		{523, 587, 659, 698, 784, 880, 988, 1047, 0},
		{1000, 0, 1500, 0, 0},
		{1000, 1000, 0, 1000, 0},
	};
	const uint32_t offsets[][9] = {
		{0, 250000, 500000, 750000, 1000000, 1250000, 1500000, 1750000, 2000000},
		{0, 200000, 300000, 500000, 600000},
		{0, 200000, 400000, 500000, 700000},
	};
	const size_t counts[] = {9, 5, 5};
	struct playback_trace_entry entry;

	start_player();
	for (int track = 0; track < 3; track++) {
		zassert_ok(playback_trace_begin(clock_us()));
		zassert_ok(cmd_post(CMD_SELECT, track));
		(void)await_state(PLAYER_PLAYING, track);
		struct player_snapshot snap = await_state(PLAYER_STOPPED, track);

		zassert_equal(offsets[track][counts[track] - 1], snap.base_us);
		zassert_equal(snap.total_us, snap.base_us);
		zassert_ok(playback_trace_end(clock_us()));
		/* A seleção silencia em [0]; em [1] começa o primeiro tom. */
		zassert_equal(1, playback_trace_read(1, &entry));
		uint64_t first = entry.elapsed_us;

		for (size_t i = 0; i < counts[track]; i++) {
			zassert_equal(1, playback_trace_read(i + 1, &entry));
			zassert_equal(frequencies[track][i], entry.freq_hz);
			zassert_equal(offsets[track][i], entry.elapsed_us - first);
		}
	}

	zassert_ok(playback_trace_begin(clock_us()));
	zassert_ok(cmd_post(CMD_PLAY_PAUSE, TRACK_NONE)); /* Reinício após fim. */
	(void)await_state(PLAYER_PLAYING, 2);
	k_msleep(50);
	zassert_ok(cmd_post(CMD_PLAY_PAUSE, TRACK_NONE));
	struct player_snapshot paused = await_state(PLAYER_PAUSED, 2);

	k_msleep(100);
	struct player_snapshot still;

	snapshot_read(&still);
	zassert_equal(paused.base_us, still.base_us);
	zassert_ok(cmd_post(CMD_PLAY_PAUSE, TRACK_NONE));
	struct player_snapshot resumed = await_state(PLAYER_PLAYING, 2);

	zassert_true(snapshot_elapsed_us(&resumed, clock_us()) < paused.base_us);
	(void)await_state(PLAYER_STOPPED, 2);
	zassert_ok(playback_trace_end(clock_us()));
	zassert_equal(1, playback_trace_read(1, &entry));
	zassert_equal(0, entry.freq_hz); /* Pausa é silêncio real no registro. */
	uint64_t pause_time = entry.elapsed_us;

	zassert_equal(1, playback_trace_read(2, &entry));
	zassert_equal(1000, entry.freq_hz);
	zassert_true(entry.elapsed_us - pause_time >= 100000);

	zassert_ok(playback_trace_begin(clock_us()));
	zassert_ok(cmd_post(CMD_SELECT, 0));
	(void)await_state(PLAYER_PLAYING, 0);
	zassert_ok(cmd_post(CMD_NEXT, TRACK_NONE));
	(void)await_state(PLAYER_PLAYING, 1);
	zassert_ok(cmd_post(CMD_PREV, TRACK_NONE));
	(void)await_state(PLAYER_PLAYING, 0);
	zassert_ok(cmd_post(CMD_STOP, TRACK_NONE));
	struct player_snapshot stopped = await_state(PLAYER_STOPPED, 0);

	zassert_equal(0, stopped.base_us);
	zassert_ok(playback_trace_end(clock_us()));
}

ZTEST_SUITE(playback, NULL, setup, NULL, NULL, teardown);

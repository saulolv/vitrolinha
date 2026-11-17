/** @file main.c
 * @brief Três testes do fluxo CSV -> loader -> player -> captura no host.
 */
#define _POSIX_C_SOURCE 200809L
#include <vitrolinha/cmd.h>
#include <vitrolinha/loader.h>
#include <vitrolinha/playback_trace.h>
#include <vitrolinha/player.h>
#include <vitrolinha/snapshot.h>
#include <vitrolinha/storage.h>
#include <vitrolinha/tone.h>

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
	zassert_ok(setenv("VITROLINHA_MUSIC_DIR", directory, 1));
	zassert_equal(5, storage_scan(tracks, ARRAY_SIZE(tracks)));
	zassert_ok(loader_init());
	zassert_ok(tone_init());
	return NULL;
}

static void teardown(void *fixture)
{
	ARG_UNUSED(fixture);
	for (int i = 0; i < 5; i++) {
		char name[8];

		snprintf(name, sizeof(name), "%d.csv", i);
		file_path(name);
		/* O teste de reinício remove 4.csv antes de pedir a faixa em RAM. */
		zassert_true(unlink(path) == 0 || errno == ENOENT);
	}
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

	cmd_flush();
	zassert_ok(player_init(5));
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

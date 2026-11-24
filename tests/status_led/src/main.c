/** @file main.c
 * @brief LEDs de estado (RF11), dirigidos pelo player de verdade.
 *
 * O player toca CSVs de uma pasta pela captura do simulador, publica o
 * instantâneo, e a thread dos LEDs o lê e escreve na fita de mentira do
 * app.overlay — que guarda as cores em vez de mandá-las a um WS2812. O que
 * se confere é a cor que chegou à fita em cada estado.
 */
#define _POSIX_C_SOURCE 200809L
#include <vitrolinha/cmd.h>
#include <vitrolinha/library.h>
#include <vitrolinha/loader.h>
#include <vitrolinha/playback_trace.h>
#include <vitrolinha/player.h>
#include <vitrolinha/snapshot.h>
#include <vitrolinha/status_led.h>
#include <vitrolinha/tone.h>

#include "fake_strip.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <zephyr/ztest.h>

#define STRIP DEVICE_DT_GET(DT_ALIAS(led_strip))

/** @brief Folga para a thread dos LEDs ver o instantâneo novo e escrever. */
#define SETTLE_MS (2 * STATUS_LED_PERIOD_MS)

static char directory[] = "/tmp/vitrolinha-leds-XXXXXX";
static char path[128];

static const char *const files[][2] = {
	/* Longa o bastante para pausar no meio: 40 notas de 100 ms. */
	{"0.csv", NULL},
	/* Duração zero na linha 2: recusada na carga, com erro. */
	{"1.csv", "frequency_hz,duration_ms\n1000,0\n"},
};

static const struct led_rgb off = {0};
static const struct led_rgb green = {.g = STATUS_LED_LEVEL};
static const struct led_rgb amber = {.r = STATUS_LED_LEVEL, .g = STATUS_LED_LEVEL / 2U};
static const struct led_rgb red = {.r = STATUS_LED_LEVEL};

static uint64_t clock_us(void)
{
	return k_cyc_to_us_floor64(k_cycle_get_64());
}

static void file_path(const char *name)
{
	snprintf(path, sizeof(path), "%s/%s", directory, name);
}

static void *setup(void)
{
	zassert_not_null(mkdtemp(directory));
	file_path(files[0][0]);
	FILE *file = fopen(path, "w");

	zassert_not_null(file);
	fputs("frequency_hz,duration_ms\n", file);
	for (int i = 0; i < 40; i++) {
		fputs("1000,100\n", file);
	}
	zassert_ok(fclose(file));
	file_path(files[1][0]);
	file = fopen(path, "w");
	zassert_not_null(file);
	fputs(files[1][1], file);
	zassert_ok(fclose(file));
	zassert_ok(setenv("VITROLINHA_MUSIC_DIR", directory, 1));
	zassert_ok(loader_init());
	zassert_ok(tone_init());
	zassert_ok(player_init());
	return NULL;
}

static void teardown(void *fixture)
{
	ARG_UNUSED(fixture);
	for (size_t i = 0; i < ARRAY_SIZE(files); i++) {
		file_path(files[i][0]);
		zassert_ok(unlink(path));
	}
	zassert_ok(rmdir(directory));
	zassert_ok(unsetenv("VITROLINHA_MUSIC_DIR"));
}

static void await_state(enum player_state wanted, int track)
{
	struct player_snapshot snap;

	for (int i = 0; i < 2000; i++) {
		snapshot_read(&snap);
		if (snap.state == wanted && snap.track == track) {
			return;
		}
		k_msleep(1);
	}
	zassert_true(false, "esperado estado %d, faixa %d; obtido %u, %d", wanted, track,
		     snap.state, snap.track);
}

/** @brief Os quatro LEDs da fita estão em @p color. */
static void expect_strip(const struct led_rgb *color, const char *what)
{
	struct led_rgb leds[FAKE_STRIP_LENGTH];

	k_msleep(SETTLE_MS);
	zassert_equal(FAKE_STRIP_LENGTH, fake_strip_last(leds), "%s: a fita inteira", what);
	for (size_t i = 0; i < FAKE_STRIP_LENGTH; i++) {
		zassert_true(leds[i].r == color->r && leds[i].g == color->g &&
				     leds[i].b == color->b,
			     "%s: LED %zu em %u,%u,%u", what, i, leds[i].r, leds[i].g, leds[i].b);
	}
}

ZTEST(status_led, test_cor_de_cada_estado)
{
	struct player_snapshot snap = SNAPSHOT_IDLE_INITIALIZER;
	struct led_rgb c;

	c = status_led_color(&snap, LIBRARY_READY);
	zassert_true(c.r == 0 && c.g == 0 && c.b == 0, "parado: apagado");
	c = status_led_color(&snap, LIBRARY_REMOVED);
	zassert_equal(STATUS_LED_LEVEL, c.r, "sem biblioteca: vermelho");
	snap.load_error = -EINVAL;
	c = status_led_color(&snap, LIBRARY_READY);
	zassert_true(c.r == STATUS_LED_LEVEL && c.g == 0, "erro de carga: vermelho");
	/* Tocando com o cartão fora: a música segue, e o verde também. */
	snap.load_error = 0;
	snap.state = PLAYER_PLAYING;
	c = status_led_color(&snap, LIBRARY_REMOVED);
	zassert_true(c.r == 0 && c.g == STATUS_LED_LEVEL, "tocando: verde");
	snap.state = PLAYER_PAUSED;
	c = status_led_color(&snap, LIBRARY_READY);
	zassert_true(c.r == STATUS_LED_LEVEL && c.g == STATUS_LED_LEVEL / 2U, "pausado: ambar");
}

ZTEST(status_led, test_sequencia_parado_tocando_pausado_erro)
{
	/* A fita ainda não inicializou: a thread não parte. */
	library_reset();
	zassert_equal(-ENODEV, status_led_init());
	zassert_ok(device_init(STRIP));
	zassert_ok(status_led_init());

	/* Boot sem cartão: vermelho. */
	expect_strip(&red, "sem cartao");

	/* Cartão lido, nada tocando: apagado. */
	zassert_equal(2, library_refresh());
	expect_strip(&off, "parado");

	zassert_ok(playback_trace_begin(clock_us()));
	zassert_ok(cmd_post(CMD_SELECT, 0));
	await_state(PLAYER_PLAYING, 0);
	expect_strip(&green, "tocando");

	zassert_ok(cmd_post(CMD_PLAY_PAUSE, TRACK_NONE));
	await_state(PLAYER_PAUSED, 0);
	expect_strip(&amber, "pausado");

	/* Arquivo recusado na carga: o player para com o erro publicado. */
	zassert_ok(cmd_post(CMD_SELECT, 1));
	k_msleep(5);
	struct player_snapshot snap;

	snapshot_read(&snap);
	zassert_equal(-EINVAL, snap.load_error);
	expect_strip(&red, "erro de carga");

	/* A cor que não muda não é reescrita. */
	unsigned int before = fake_strip_updates();

	k_msleep(3 * STATUS_LED_PERIOD_MS);
	zassert_equal(before, fake_strip_updates(), "reescreveu a mesma cor");

	/* Fita que falha: a thread tenta de novo a cada passada, e acerta
	 * quando a fita volta.
	 */
	fake_strip_set_result(-EIO);
	zassert_ok(cmd_post(CMD_SELECT, 0));
	await_state(PLAYER_PLAYING, 0);
	before = fake_strip_updates();
	k_msleep(3 * STATUS_LED_PERIOD_MS);
	zassert_true(fake_strip_updates() >= before + 2U, "nao tentou de novo");
	fake_strip_set_result(0);
	expect_strip(&green, "fita de volta");

	zassert_ok(cmd_post(CMD_STOP, TRACK_NONE));
	await_state(PLAYER_STOPPED, 0);
	expect_strip(&off, "parado de novo");
	zassert_ok(playback_trace_end(clock_us()));
}

ZTEST_SUITE(status_led, NULL, setup, NULL, NULL, teardown);

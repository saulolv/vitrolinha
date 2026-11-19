/**
 * @file main.c
 * @brief Testes da interface: lista, clique e barra de progresso.
 *
 * A interface roda sobre um display de teste e um keypad que recebe teclas
 * pelo subsistema `input`, como no simulador. A biblioteca é uma pasta
 * temporária lida pelo `storage_host`, e o "player" é o próprio teste,
 * publicando instantâneos. O laço do LVGL também é do teste: nada roda sem
 * lv_timer_handler() ou lv_indev_read().
 */
#define _POSIX_C_SOURCE 200809L

#include <vitrolinha/cmd.h>
#include <vitrolinha/display.h>
#include <vitrolinha/snapshot.h>
#include <vitrolinha/storage.h>
#include <vitrolinha/track.h>

#include "fake_display.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <lvgl.h>
#include <lvgl_input_device.h>
#include <lvgl_zephyr.h>

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/** Faixas da pasta "tres": a ordem da lista é a alfabética. */
static const char *const three[] = {"a", "b", "c"};

/** Uma a mais do que a biblioteca comporta. */
#define FULL_COUNT (LIBRARY_MAX + 1U)

static const struct device *const display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *const keypad_dev = DEVICE_DT_GET(DT_NODELABEL(lvgl_keypad));

static char directory[] = "/tmp/vitrolinha-display-XXXXXX";
static char path[160];

/** Os objetos que display_init() acabou de criar. */
struct ui {
	lv_obj_t *roller;
	lv_obj_t *label;
	lv_obj_t *bar;
};

static void make_path(const char *dir, const char *name)
{
	if (name == NULL) {
		snprintf(path, sizeof(path), "%s/%s", directory, dir);
	} else {
		snprintf(path, sizeof(path), "%s/%s/%s", directory, dir, name);
	}
}

static void write_track(const char *dir, const char *name)
{
	char file[32];

	snprintf(file, sizeof(file), "%s.csv", name);
	make_path(dir, file);

	FILE *f = fopen(path, "w");

	zassert_not_null(f);
	fputs("frequency_hz,duration_ms\n440,100\n", f);
	zassert_ok(fclose(f));
}

static void remove_track(const char *dir, const char *name)
{
	char file[32];

	snprintf(file, sizeof(file), "%s.csv", name);
	make_path(dir, file);
	zassert_ok(unlink(path));
}

static void full_name(char *out, size_t size, size_t i)
{
	snprintf(out, size, "f%02zu", i);
}

/**
 * @brief Aponta a biblioteca para @p dir e remonta a tela do zero.
 *
 * @param dir Subpasta do diretório temporário; pode não existir.
 * @return Os objetos da tela nova.
 */
static struct ui start_ui(const char *dir)
{
	lv_obj_t *screen = lv_screen_active();
	lv_obj_t *container;
	struct ui ui;

	make_path(dir, NULL);
	zassert_ok(setenv("VITROLINHA_MUSIC_DIR", path, 1));

	lv_obj_clean(screen);
	zassert_ok(display_init());

	ui.roller = lv_obj_get_child(screen, 0);
	container = lv_obj_get_child(screen, 1);
	ui.label = lv_obj_get_child(container, 0);
	ui.bar = lv_obj_get_child(container, 1);

	zassert_not_null(ui.roller);
	zassert_not_null(ui.label);
	zassert_not_null(ui.bar);
	return ui;
}

/** @brief Pressiona e solta uma tecla, e deixa o LVGL processar as duas. */
static void tap(uint16_t code)
{
	zassert_ok(input_report_key(NULL, code, 1, true, K_FOREVER));
	zassert_ok(input_report_key(NULL, code, 0, true, K_FOREVER));
	lv_indev_read(lvgl_input_get_indev(keypad_dev));
}

/** @brief Deixa passar um período da interface e roda os timers do LVGL. */
static void refresh(void)
{
	k_msleep(DISPLAY_REFRESH_PERIOD_MS + 10);
	lv_timer_handler();
}

static void publish(uint32_t state, int16_t track, uint64_t base_us, uint32_t total_us)
{
	const struct player_snapshot snap = {
		.state = state,
		.base_us = base_us,
		.total_us = total_us,
		.track = track,
	};

	snapshot_publish(&snap);
}

static uint64_t now_us(void)
{
	return k_ticks_to_us_floor64(k_uptime_ticks());
}

static void *setup(void)
{
	zassert_not_null(mkdtemp(directory));

	make_path("tres", NULL);
	zassert_ok(mkdir(path, 0700));
	ARRAY_FOR_EACH(three, i) {
		write_track("tres", three[i]);
	}

	make_path("vazia", NULL);
	zassert_ok(mkdir(path, 0700));

	make_path("cheia", NULL);
	zassert_ok(mkdir(path, 0700));
	for (size_t i = 0; i < FULL_COUNT; i++) {
		char name[8];

		full_name(name, sizeof(name), i);
		write_track("cheia", name);
	}

	/* Antes de o display inicializar, a interface se recusa a subir. */
	zassert_false(device_is_ready(display_dev));
	zassert_equal(-ENODEV, display_init());

	zassert_ok(device_init(display_dev));
	zassert_ok(lvgl_init());
	return NULL;
}

static void teardown(void *fixture)
{
	ARG_UNUSED(fixture);

	ARRAY_FOR_EACH(three, i) {
		remove_track("tres", three[i]);
	}
	for (size_t i = 0; i < FULL_COUNT; i++) {
		char name[8];

		full_name(name, sizeof(name), i);
		remove_track("cheia", name);
	}
	static const char *const dirs[] = {"tres", "vazia", "cheia"};

	ARRAY_FOR_EACH(dirs, i) {
		make_path(dirs[i], NULL);
		zassert_ok(rmdir(path));
	}
	zassert_ok(rmdir(directory));
	zassert_ok(unsetenv("VITROLINHA_MUSIC_DIR"));
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	const struct player_snapshot idle = SNAPSHOT_IDLE_INITIALIZER;

	snapshot_publish(&idle);
	cmd_flush();
	fake_display_set_blanking_off_result(0);
}

ZTEST_SUITE(display, NULL, setup, before, NULL, teardown);

ZTEST(display, test_lista_as_faixas_da_varredura_em_ordem)
{
	struct ui ui = start_ui("tres");

	zassert_str_equal("a\nb\nc", lv_roller_get_options(ui.roller));
	zassert_equal(0, lv_roller_get_selected(ui.roller));
	zassert_str_equal("Nada tocando", lv_label_get_text(ui.label));
	zassert_equal(0, lv_bar_get_value(ui.bar));
}

ZTEST(display, test_biblioteca_ausente_mostra_o_erro_da_varredura)
{
	struct ui ui = start_ui("nao-existe");

	zassert_str_equal(storage_error_text(-ENODEV), lv_roller_get_options(ui.roller));
}

ZTEST(display, test_biblioteca_vazia_diz_que_esta_vazia)
{
	struct ui ui = start_ui("vazia");

	zassert_str_equal("Biblioteca vazia", lv_roller_get_options(ui.roller));
}

ZTEST(display, test_biblioteca_cheia_mostra_so_as_32_primeiras)
{
	struct ui ui = start_ui("cheia");
	char last[8];
	const char *options;

	full_name(last, sizeof(last), LIBRARY_MAX - 1U);
	options = lv_roller_get_options(ui.roller);

	zassert_equal(LIBRARY_MAX, lv_roller_get_option_count(ui.roller));
	zassert_not_null(strstr(options, last));
	zassert_is_null(strstr(options, "f32"));
}

ZTEST(display, test_girar_e_clicar_pede_a_faixa_selecionada)
{
	struct ui ui = start_ui("tres");
	struct cmd received;

	tap(INPUT_KEY_RIGHT);
	zassert_equal(1, lv_roller_get_selected(ui.roller));

	tap(INPUT_KEY_ENTER);
	zassert_ok(cmd_get(&received, K_NO_WAIT));
	zassert_equal(CMD_SELECT, received.kind);
	zassert_equal(1, received.track);

	/* O clique não tira o roller da edição: o giro continua selecionando. */
	zassert_true(lv_group_get_editing(lv_obj_get_group(ui.roller)));
	zassert_equal(-ENOMSG, cmd_get(&received, K_NO_WAIT));
}

ZTEST(display, test_clique_fora_do_grupo_ainda_pede_a_faixa)
{
	struct ui ui = start_ui("tres");
	struct cmd received;

	lv_group_remove_obj(ui.roller);
	lv_obj_send_event(ui.roller, LV_EVENT_CLICKED, NULL);

	zassert_ok(cmd_get(&received, K_NO_WAIT));
	zassert_equal(CMD_SELECT, received.kind);
	zassert_equal(0, received.track);
}

ZTEST(display, test_clique_em_biblioteca_vazia_nao_pede_nada)
{
	struct ui ui = start_ui("vazia");
	struct cmd received;

	lv_obj_send_event(ui.roller, LV_EVENT_CLICKED, NULL);
	zassert_equal(-ENOMSG, cmd_get(&received, K_NO_WAIT));
}

ZTEST(display, test_clique_com_fila_cheia_descarta_a_selecao)
{
	struct ui ui = start_ui("tres");
	struct cmd received;

	for (size_t i = 0; i < CMD_QUEUE_DEPTH; i++) {
		zassert_ok(cmd_post(CMD_PLAY_PAUSE, TRACK_NONE));
	}

	lv_obj_send_event(ui.roller, LV_EVENT_CLICKED, NULL);

	for (size_t i = 0; i < CMD_QUEUE_DEPTH; i++) {
		zassert_ok(cmd_get(&received, K_NO_WAIT));
		zassert_equal(CMD_PLAY_PAUSE, received.kind);
	}
	zassert_equal(-ENOMSG, cmd_get(&received, K_NO_WAIT));
}

ZTEST(display, test_barra_e_rotulo_seguem_o_instantaneo)
{
	struct ui ui = start_ui("tres");

	/*
	 * Faixa de 200 ms que começa agora: depois do período que a tela espera,
	 * ~110 ms, a barra está pouco além da metade. Base no presente, e não no
	 * passado: logo depois do boot o relógio ainda não passou de 1 s.
	 */
	publish(PLAYER_PLAYING, 1, now_us(), 200U * USEC_PER_MSEC);
	refresh();
	zassert_str_equal("b", lv_label_get_text(ui.label));
	zassert_within(lv_bar_get_value(ui.bar), 550, 50);

	/* Sem publicação nova a barra anda sozinha, pelo relógio: o player não
	 * precisa publicar a cada nota para o progresso avançar.
	 */
	refresh();
	zassert_str_equal("b", lv_label_get_text(ui.label));
	zassert_true(lv_bar_get_value(ui.bar) > 900, "a barra parou sem publicacao nova");

	/* Mesma faixa, pausada no fim: o rótulo fica, a barra vai a 1000. */
	publish(PLAYER_PAUSED, 1, 2U * USEC_PER_SEC, 2U * USEC_PER_SEC);
	refresh();
	zassert_str_equal("b", lv_label_get_text(ui.label));
	zassert_equal(1000, lv_bar_get_value(ui.bar));

	/* Faixa fora da biblioteca não tem nome para mostrar. */
	publish(PLAYER_STOPPED, (int16_t)LIBRARY_MAX, 0U, 0U);
	refresh();
	zassert_str_equal("Nada tocando", lv_label_get_text(ui.label));

	publish(PLAYER_PLAYING, 2, now_us(), USEC_PER_SEC);
	refresh();
	zassert_str_equal("c", lv_label_get_text(ui.label));

	publish(PLAYER_STOPPED, TRACK_NONE, 0U, 0U);
	refresh();
	zassert_str_equal("Nada tocando", lv_label_get_text(ui.label));
	zassert_equal(0, lv_bar_get_value(ui.bar));
}

ZTEST(display, test_display_sem_blanking_ainda_sobe)
{
	fake_display_set_blanking_off_result(-ENOSYS);
	zassert_ok(display_init());
}

ZTEST(display, test_falha_ao_desligar_o_blanking_e_devolvida)
{
	fake_display_set_blanking_off_result(-EIO);
	zassert_equal(-EIO, display_init());
}

/**
 * @file main.c
 * @brief Testes da interface: biblioteca, reprodução e a troca entre elas.
 *
 * A interface roda sobre um display de teste e um keypad que recebe teclas
 * pelo subsistema `input`, como no simulador. A biblioteca é uma pasta
 * temporária lida pelo `storage_host` através da `library`, e o "player" é
 * o próprio teste,
 * publicando instantâneos. O laço do LVGL também é do teste: nada roda sem
 * lv_timer_handler() ou lv_indev_read().
 */
#define _POSIX_C_SOURCE 200809L

#include <vitrolinha/cmd.h>
#include <vitrolinha/cmd_test.h>
#include <vitrolinha/display.h>
#include <vitrolinha/library.h>
#include <vitrolinha/library_test.h>
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

/** Faixas da pasta "longos": o nome do cabeçalho passa das 16 colunas. */
static const struct {
	const char *file;
	const char *header;
} long_names[] = {
	{"a.rtx", "Happy Birthday"},
	{"b.rtx", "Twinkle Twinkle Little Star"},
	{"c.rtx", "Eine kleine Nachtmusik"},
};

/** Uma a mais do que a biblioteca comporta. */
#define FULL_COUNT (LIBRARY_MAX + 1U)

static const struct device *const display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *const keypad_dev = DEVICE_DT_GET(DT_NODELABEL(lvgl_keypad));

static char directory[] = "/tmp/vitrolinha-display-XXXXXX";
static char path[160];

/** Os objetos que display_init() acabou de criar, nas duas telas. */
struct ui {
	lv_obj_t *library;
	lv_obj_t *list;
	lv_obj_t *mini_bar;
	lv_obj_t *mini_row;
	lv_obj_t *divider;
	lv_obj_t *mini_icon;
	lv_obj_t *mini_name;
	lv_obj_t *mini_time;
	lv_obj_t *playback;
	lv_obj_t *name;
	lv_obj_t *state;
	lv_obj_t *time;
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

/** @brief Deixa passar um período da interface e roda os timers do LVGL. */
static void refresh(void)
{
	k_msleep(DISPLAY_REFRESH_PERIOD_MS + 10);
	lv_timer_handler();
}

/** @brief Pressiona e solta uma tecla, e deixa o LVGL processar as duas. */
static void tap(uint16_t code)
{
	zassert_ok(input_report_key(NULL, code, 1, true, K_FOREVER));
	zassert_ok(input_report_key(NULL, code, 0, true, K_FOREVER));
	lv_indev_read(lvgl_input_get_indev(keypad_dev));
}

/** @brief Aperta o botão de alternar telas e espera o timer atendê-lo. */
static void toggle(void)
{
	zassert_ok(input_report_key(NULL, DISPLAY_TOGGLE_BUTTON, 1, true, K_FOREVER));
	zassert_ok(input_report_key(NULL, DISPLAY_TOGGLE_BUTTON, 0, true, K_FOREVER));
	refresh();
}

/**
 * @brief Aponta a biblioteca para @p dir e a varre, como o monitor do
 *        cartão faria numa inserção.
 *
 * @param dir Subpasta do diretório temporário; pode não existir, e aí a
 *            varredura é a de um soquete vazio.
 */
static void insert_card(const char *dir)
{
	make_path(dir, NULL);
	zassert_ok(setenv("VITROLINHA_MUSIC_DIR", path, 1));
	(void)library_refresh();
}

/**
 * @brief Aponta a biblioteca para @p dir e remonta as telas do zero.
 *
 * Passa pela reprodução para guardar os objetos dela, e volta: a interface
 * sobe na biblioteca, e é lá que cada teste começa.
 *
 * @param dir Subpasta do diretório temporário; pode não existir.
 * @return Os objetos das telas novas.
 */
static struct ui start_ui(const char *dir)
{
	struct ui ui;

	insert_card(dir);
	zassert_ok(display_init());

	ui.library = lv_screen_active();
	ui.list = lv_obj_get_child(ui.library, 0);
	/* O filho 1 é a barra de rolagem da lista. */
	ui.mini_bar = lv_obj_get_child(ui.library, 2);
	ui.mini_row = lv_obj_get_child(ui.library, 3);
	ui.divider = lv_obj_get_child(ui.library, 4);
	ui.mini_icon = lv_obj_get_child(ui.mini_row, 0);
	ui.mini_name = lv_obj_get_child(ui.mini_row, 1);
	ui.mini_time = lv_obj_get_child(ui.mini_row, 2);

	toggle();
	ui.playback = lv_screen_active();
	zassert_not_equal(ui.library, ui.playback, "o botao nao abriu a reproducao");
	ui.name = lv_obj_get_child(ui.playback, 0);
	ui.state = lv_obj_get_child(ui.playback, 1);
	ui.time = lv_obj_get_child(ui.playback, 2);
	ui.bar = lv_obj_get_child(ui.playback, 3);

	toggle();
	zassert_equal(ui.library, lv_screen_active());

	zassert_not_null(ui.list);
	zassert_not_null(ui.mini_time);
	zassert_not_null(ui.name);
	zassert_not_null(ui.state);
	zassert_not_null(ui.time);
	zassert_not_null(ui.bar);
	return ui;
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

/** @brief Publica uma carga recusada, como o player faz ao recebê-la. */
static void publish_error(int16_t track, int16_t err, uint16_t line, uint16_t column)
{
	const struct player_snapshot snap = {
		.state = PLAYER_STOPPED,
		.track = track,
		.load_error = err,
		.error_line = line,
		.error_column = column,
	};

	snapshot_publish(&snap);
}

static uint64_t now_us(void)
{
	return k_ticks_to_us_floor64(k_uptime_ticks());
}

/** @brief Uma linha do ícone do mini-player: distingue tocar, pausar e parar. */
static uint8_t icon_row(lv_obj_t *icon, size_t row)
{
	const lv_image_dsc_t *dsc = lv_image_get_src(icon);

	/* Depois dos 8 bytes de paleta, um byte por linha. */
	return dsc->data[8U + row];
}

static bool hidden(lv_obj_t *obj)
{
	return lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Os textos das linhas visíveis da lista, um por linha.
 *
 * O texto pedido, guardado no user_data do rótulo: o que o LVGL guarda ele
 * reescreve com reticências quando o nome não cabe.
 */
static const char *list_text(const struct ui *ui)
{
	static char text[8 * (TRACK_NAME_MAX + 3U)];
	size_t used = 0;

	text[0] = '\0';
	for (uint32_t i = 0; i < lv_obj_get_child_count(ui->list); i++) {
		lv_obj_t *row = lv_obj_get_child(ui->list, i);

		if (!hidden(row)) {
			used += snprintf(&text[used], sizeof(text) - used, "%s%s",
					 (used > 0U) ? "\n" : "", (const char *)lv_obj_get_user_data(row));
		}
	}
	return text;
}

/** @brief Posição, entre as linhas visíveis, da linha em destaque; -1 se nenhuma. */
static int focused(const struct ui *ui)
{
	for (uint32_t i = 0; i < lv_obj_get_child_count(ui->list); i++) {
		if (lv_obj_has_state(lv_obj_get_child(ui->list, i), LV_STATE_CHECKED)) {
			return (int)i;
		}
	}
	return -1;
}

/** @brief Texto da linha em destaque. */
static const char *focused_text(const struct ui *ui)
{
	return lv_obj_get_user_data(lv_obj_get_child(ui->list, focused(ui)));
}

/** @brief Desenha a tela à mostra no display de teste. */
static void draw(void)
{
	lv_obj_invalidate(lv_screen_active());
	lv_refr_now(NULL);
}

/** @brief Diz se alguma coluna da linha @p y está acesa no OLED. */
static bool row_lit(int y)
{
	for (int x = 0; x < 128; x++) {
		if (fake_display_lit(x, y)) {
			return true;
		}
	}
	return false;
}

static void expect_no_cmd(void)
{
	struct cmd received;

	zassert_equal(-ENOMSG, cmd_get(&received, K_NO_WAIT), "sobrou comando na fila");
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

	/* Nomes de cabeçalho RTTTL maiores que as 16 colunas da tela. */
	make_path("longos", NULL);
	zassert_ok(mkdir(path, 0700));
	ARRAY_FOR_EACH(long_names, i) {
		make_path("longos", long_names[i].file);
		FILE *f = fopen(path, "w");

		zassert_not_null(f);
		fprintf(f, "%s:d=4,o=5,b=120:c,e,g", long_names[i].header);
		zassert_ok(fclose(f));
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
	ARRAY_FOR_EACH(long_names, i) {
		make_path("longos", long_names[i].file);
		zassert_ok(unlink(path));
	}
	static const char *const dirs[] = {"tres", "vazia", "cheia", "longos"};

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
	/* Cada caso começa do boot: nenhum cartão lido ainda. */
	library_reset();
}

ZTEST_SUITE(display, NULL, setup, before, NULL, teardown);

/* -------------------------------------------------------------------------- */
/* Biblioteca                                                                 */
/* -------------------------------------------------------------------------- */

ZTEST(display, test_lista_as_faixas_da_varredura_em_ordem)
{
	struct ui ui = start_ui("tres");

	zassert_str_equal("  a\n  b\n  c", list_text(&ui));
	zassert_equal(0, focused(&ui));
	zassert_str_equal("Nada tocando", lv_label_get_text(ui.name));
	zassert_str_equal("Parado", lv_label_get_text(ui.state));
	zassert_str_equal("0:00 / 0:00", lv_label_get_text(ui.time));
	zassert_equal(0, lv_bar_get_value(ui.bar));
}

ZTEST(display, test_biblioteca_ausente_mostra_o_estado_do_cartao)
{
	struct ui ui = start_ui("nao-existe");

	zassert_str_equal(library_state_text(LIBRARY_NO_CARD), list_text(&ui));
}

ZTEST(display, test_cartao_entra_e_sai_com_a_tela_no_ar)
{
	struct ui ui = start_ui("nao-existe");

	/* Inserido depois do boot: a lista aparece sem remontar as telas. */
	insert_card("tres");
	refresh();
	zassert_str_equal("  a\n  b\n  c", list_text(&ui));

	/* Removido tocando: a lista diz por quê, e a faixa em RAM segue com
	 * o nome dela na reprodução e no mini-player.
	 */
	tap(INPUT_KEY_RIGHT);
	publish(PLAYER_PLAYING, 1, now_us(), USEC_PER_SEC);
	refresh();
	insert_card("nao-existe");
	refresh();
	zassert_str_equal(library_state_text(LIBRARY_REMOVED), list_text(&ui));
	zassert_str_equal("b", lv_label_get_text(ui.name));
	zassert_str_equal("b", lv_label_get_text(ui.mini_name));

	/* Sem lista, o clique não pede nada. */
	tap(INPUT_KEY_ENTER);
	expect_no_cmd();

	/* De volta, o foco fica onde estava. */
	insert_card("tres");
	refresh();
	zassert_str_equal("  a\n> b\n  c", list_text(&ui));
	zassert_equal(1, focused(&ui));
}

ZTEST(display, test_lista_menor_traz_o_foco_para_dentro)
{
	struct ui ui = start_ui("cheia");

	for (size_t i = 0; i < FULL_COUNT; i++) {
		tap(INPUT_KEY_RIGHT);
	}
	zassert_equal(3, focused(&ui));

	/* Outro cartão, com três faixas: o foco vai para a última, e a
	 * janela volta ao começo.
	 */
	insert_card("tres");
	refresh();
	zassert_str_equal("  a\n  b\n  c", list_text(&ui));
	zassert_equal(2, focused(&ui));

	/* De volta à lista longa, a janela mostra a faixa em foco sem rolar. */
	insert_card("cheia");
	refresh();
	zassert_str_equal("  f00\n  f01\n  f02\n  f03", list_text(&ui));
	zassert_equal(2, focused(&ui));

	/* Sem cartão, nada fica em foco. */
	insert_card("nao-existe");
	refresh();
	zassert_str_equal(library_state_text(LIBRARY_REMOVED), list_text(&ui));
}

ZTEST(display, test_biblioteca_vazia_diz_que_esta_vazia)
{
	struct ui ui = start_ui("vazia");

	/* Uma faixa no instantâneo não põe marcador numa lista sem faixas. */
	publish(PLAYER_PLAYING, 0, now_us(), USEC_PER_SEC);
	refresh();
	zassert_str_equal("Nenhuma faixa", list_text(&ui));
}

ZTEST(display, test_biblioteca_cheia_mostra_so_as_32_primeiras)
{
	struct ui ui = start_ui("cheia");

	/* Quatro linhas na tela, as primeiras da biblioteca. */
	zassert_str_equal("  f00\n  f01\n  f02\n  f03", list_text(&ui));

	/* Indo até o fim, a última é a 32.ª: a 33.ª ficou de fora. */
	for (size_t i = 0; i < FULL_COUNT + 5U; i++) {
		tap(INPUT_KEY_RIGHT);
	}
	zassert_str_equal("  f28\n  f29\n  f30\n  f31", list_text(&ui));
	zassert_equal(3, focused(&ui));
}

ZTEST(display, test_girar_e_clicar_toca_e_abre_a_reproducao)
{
	struct ui ui = start_ui("tres");
	struct cmd received;

	tap(INPUT_KEY_RIGHT);
	zassert_equal(1, focused(&ui));
	zassert_equal(ui.library, lv_screen_active(), "girar nao troca de tela");

	tap(INPUT_KEY_ENTER);
	zassert_ok(cmd_get(&received, K_NO_WAIT));
	zassert_equal(CMD_SELECT, received.kind);
	zassert_equal(1, received.track);
	expect_no_cmd();
	zassert_equal(ui.playback, lv_screen_active());

	/* De volta à lista, o destaque está onde ficou, e a lista segue em
	 * edição: é assim que o giro do encoder chega a ela como seta.
	 */
	toggle();
	zassert_equal(1, focused(&ui));
	zassert_true(lv_group_get_editing(lv_obj_get_group(ui.list)));
}

ZTEST(display, test_faixa_lembrada_vira_o_destaque_sem_tocar)
{
	struct ui ui = start_ui("cheia");

	/* A faixa do boot (RF10) chega como pedido; a janela rola até ela. */
	display_cue(6);
	refresh();
	zassert_str_equal("  f03\n  f04\n  f05\n  f06", list_text(&ui));
	zassert_equal(3, focused(&ui));
	zassert_equal(ui.library, lv_screen_active());
	expect_no_cmd();

	/* Índice fora da lista: descartado, o destaque fica. */
	display_cue((int)LIBRARY_MAX);
	refresh();
	zassert_equal(3, focused(&ui));
	zassert_str_equal("  f03\n  f04\n  f05\n  f06", list_text(&ui));
}

ZTEST(display, test_foco_para_nas_pontas_da_lista)
{
	struct ui ui = start_ui("tres");

	/* Sem dar a volta: no topo, subir não vai para o fim. */
	tap(INPUT_KEY_LEFT);
	zassert_equal(0, focused(&ui));

	tap(INPUT_KEY_RIGHT);
	tap(INPUT_KEY_RIGHT);
	tap(INPUT_KEY_RIGHT);
	zassert_equal(2, focused(&ui));

	/* Outras teclas não movem o destaque. */
	lv_obj_send_event(ui.list, LV_EVENT_KEY, &(uint32_t){LV_KEY_ESC});
	zassert_equal(2, focused(&ui));
}

ZTEST(display, test_lista_longa_rola_nas_bordas)
{
	struct ui ui = start_ui("cheia");

	/* O destaque desce pelas linhas sem a lista se mexer. */
	for (int i = 0; i < 3; i++) {
		tap(INPUT_KEY_RIGHT);
	}
	zassert_equal(3, focused(&ui));
	zassert_str_equal("  f03", focused_text(&ui));
	zassert_str_equal("  f00\n  f01\n  f02\n  f03", list_text(&ui));

	/* Passando da última linha, a lista rola uma, e o destaque fica nela. */
	tap(INPUT_KEY_RIGHT);
	zassert_equal(3, focused(&ui));
	zassert_str_equal("  f04", focused_text(&ui));
	zassert_str_equal("  f01\n  f02\n  f03\n  f04", list_text(&ui));

	/* Subindo, o destaque sobe pelas linhas, e só rola no topo. */
	for (int i = 0; i < 3; i++) {
		tap(INPUT_KEY_LEFT);
	}
	zassert_equal(0, focused(&ui));
	zassert_str_equal("  f01", focused_text(&ui));
	tap(INPUT_KEY_LEFT);
	zassert_equal(0, focused(&ui));
	zassert_str_equal("  f00", focused_text(&ui));
}

ZTEST(display, test_clique_em_biblioteca_vazia_nao_pede_nada)
{
	struct ui ui = start_ui("vazia");

	/* A mensagem não é uma faixa: sem destaque, e girar ou clicar nada faz. */
	zassert_equal(-1, focused(&ui));
	tap(INPUT_KEY_RIGHT);
	zassert_equal(-1, focused(&ui));
	lv_obj_send_event(ui.list, LV_EVENT_CLICKED, NULL);
	tap(INPUT_KEY_ENTER);
	expect_no_cmd();
	zassert_equal(ui.library, lv_screen_active());
}

ZTEST(display, test_clique_com_fila_cheia_descarta_a_selecao)
{
	struct ui ui = start_ui("tres");
	struct cmd received;

	for (size_t i = 0; i < CMD_QUEUE_DEPTH; i++) {
		zassert_ok(cmd_post(CMD_PLAY_PAUSE, TRACK_NONE));
	}

	lv_obj_send_event(ui.list, LV_EVENT_CLICKED, NULL);

	for (size_t i = 0; i < CMD_QUEUE_DEPTH; i++) {
		zassert_ok(cmd_get(&received, K_NO_WAIT));
		zassert_equal(CMD_PLAY_PAUSE, received.kind);
	}
	expect_no_cmd();

	/* Nada foi pedido, então não há o que mostrar tocando. */
	zassert_equal(ui.library, lv_screen_active());
}

ZTEST(display, test_marcador_segue_a_faixa_sem_mover_o_foco)
{
	struct ui ui = start_ui("tres");

	tap(INPUT_KEY_RIGHT);
	tap(INPUT_KEY_RIGHT);
	zassert_equal(2, focused(&ui));

	/* Seguinte e anterior pelos botões: o `>` anda, o foco de quem navega
	 * fica onde estava.
	 */
	publish(PLAYER_PLAYING, 0, now_us(), USEC_PER_SEC);
	refresh();
	zassert_str_equal("> a\n  b\n  c", list_text(&ui));
	zassert_equal(2, focused(&ui));

	publish(PLAYER_PLAYING, 1, now_us(), USEC_PER_SEC);
	refresh();
	zassert_str_equal("  a\n> b\n  c", list_text(&ui));
	zassert_equal(2, focused(&ui));

	publish(PLAYER_STOPPED, TRACK_NONE, 0U, 0U);
	refresh();
	zassert_str_equal("  a\n  b\n  c", list_text(&ui));
}

/* -------------------------------------------------------------------------- */
/* Mini-player                                                                */
/* -------------------------------------------------------------------------- */

ZTEST(display, test_lista_e_mini_player_cabem_na_tela_sem_se_sobrepor)
{
	struct ui ui = start_ui("tres");

	int32_t line = lv_font_get_line_height(lv_obj_get_style_text_font(ui.list, 0));

	lv_obj_update_layout(ui.library);

	/* Quatro linhas inteiras da fonte no alto, a divisória e a barra abaixo
	 * da lista, o rodapé abaixo da barra com altura para uma linha sem
	 * cortar, e tudo dentro dos 64 px. Foi este teste que mostrou que a
	 * linha da UNSCII-8 tem 9 px, e não 8: com 2 px de espaçamento a lista
	 * invadia a barra.
	 */
	zassert_equal(0, lv_obj_get_y(ui.list));
	zassert_equal(4 * line, lv_obj_get_height(ui.list), "linha de %d px", line);
	zassert_true(lv_obj_get_y(ui.divider) > lv_obj_get_height(ui.list),
		     "divisoria sobre a lista");
	zassert_equal(128, lv_obj_get_width(ui.divider));
	zassert_true(lv_obj_get_y(ui.mini_bar) >
		     lv_obj_get_y(ui.divider) + lv_obj_get_height(ui.divider),
		     "barra sobre a divisoria");
	zassert_true(lv_obj_get_y(ui.mini_row) >
		     lv_obj_get_y(ui.mini_bar) + lv_obj_get_height(ui.mini_bar),
		     "rodape sobre a barra");
	zassert_true(lv_obj_get_height(ui.mini_row) >= line, "rodape corta o texto");
	zassert_equal(64, lv_obj_get_y(ui.mini_row) + lv_obj_get_height(ui.mini_row));

	/* A barra do mini-player é a da reprodução, mais baixa: mesma largura,
	 * centralizada.
	 */
	int32_t x = lv_obj_get_x(ui.mini_bar);
	int32_t width = lv_obj_get_width(ui.mini_bar);

	lv_obj_update_layout(ui.playback);
	zassert_equal(lv_obj_get_width(ui.bar), width);
	zassert_true(width < 128, "barra sem margem");
	zassert_within(128 - (x + width), x, 1, "barra fora do centro");
}

ZTEST(display, test_desenho_da_biblioteca_sem_sobras)
{
	struct ui ui = start_ui("cheia");

	/* Foco no meio da lista e uma faixa tocando: o caso mais cheio. */
	tap(INPUT_KEY_RIGHT);
	tap(INPUT_KEY_RIGHT);
	publish(PLAYER_PLAYING, 1, now_us(), 4U * USEC_PER_SEC);
	refresh();
	draw();
	fake_display_dump("biblioteca");

	int32_t list_end = lv_obj_get_y(ui.list) + lv_obj_get_height(ui.list);
	int32_t divider = lv_obj_get_y(ui.divider);
	int32_t bar_top = lv_obj_get_y(ui.mini_bar);
	int32_t bar_end = bar_top + lv_obj_get_height(ui.mini_bar);
	int32_t row_top = lv_obj_get_y(ui.mini_row);

	/* A divisória acesa de ponta a ponta, e as folgas em volta dela e da
	 * barra apagadas. Foi uma folga acesa — o contorno de foco do tema, 2 px
	 * por fora do roller — que apareceu no OLED como uma faixa colada na
	 * barra.
	 */
	for (int x = 0; x < 128; x++) {
		zassert_true(fake_display_lit(x, divider), "divisoria apagada em x=%d", x);
	}
	for (int32_t y = list_end; y < divider; y++) {
		zassert_false(row_lit(y), "folga acesa na linha %d, acima da divisoria", y);
	}
	for (int32_t y = divider + 1; y < bar_top; y++) {
		zassert_false(row_lit(y), "folga acesa na linha %d, acima da barra", y);
	}
	for (int32_t y = bar_end; y < row_top; y++) {
		zassert_false(row_lit(y), "folga acesa na linha %d, abaixo da barra", y);
	}

	/* O destaque é a linha inteira acesa, da borda esquerda até a barra de
	 * rolagem, que fica acesa na borda direita.
	 */
	lv_area_t focus;

	lv_obj_get_coords(lv_obj_get_child(ui.list, 2), &focus);
	zassert_true(fake_display_lit(focus.x1, focus.y1), "destaque nao comeca na borda");
	zassert_equal(0, focus.x1);
	zassert_true(fake_display_lit(focus.x2, focus.y1), "destaque nao chega ao fim da linha");
	zassert_false(fake_display_lit(focus.x2 + 1, focus.y1),
		      "destaque encosta na barra de rolagem");
	zassert_true(fake_display_lit(127, 0), "sem barra de rolagem na lista longa");

	toggle();
	draw();
	fake_display_dump("reproducao");
}

ZTEST(display, test_nome_longo_nao_quebra_a_tela)
{
	struct ui ui = start_ui("longos");
	int32_t line = lv_font_get_line_height(lv_obj_get_style_text_font(ui.list, 0));

	/* "> Happy Birthday" tem 16 caracteres, 128 px, numa linha de 125: com
	 * altura automática, as reticências não valem e o rótulo quebra em duas
	 * linhas, empurrando a lista. A faixa 1 tem o nome mais longo que cabe
	 * no campo, 23 bytes.
	 */
	publish(PLAYER_PLAYING, 0, now_us(), 4U * USEC_PER_SEC);
	refresh();
	draw();
	fake_display_dump("nomes longos, faixa 0 tocando");

	lv_obj_update_layout(ui.library);
	for (uint32_t i = 0; i < lv_obj_get_child_count(ui.list); i++) {
		lv_obj_t *row = lv_obj_get_child(ui.list, i);

		if (hidden(row)) {
			continue;
		}
		zassert_equal(line, lv_obj_get_height(row), "linha %u quebrou", i);
		zassert_equal((int32_t)i * line, lv_obj_get_y(row), "linha %u fora do lugar", i);
	}

	/* O LVGL guarda o nome com reticências; a cópia guarda o pedido, e é
	 * contra ela que a tela decide se redesenha. Sem a cópia, o nome longo
	 * seria reescrito a cada atualização.
	 */
	lv_obj_t *first = lv_obj_get_child(ui.list, 0);

	zassert_str_equal("> Happy Birthday", lv_obj_get_user_data(first));
	zassert_not_null(strstr(lv_label_get_text(first), "..."), "sem reticencias");

	publish(PLAYER_PLAYING, 1, now_us(), 4U * USEC_PER_SEC);
	refresh();
	draw();
	fake_display_dump("nomes longos, faixa 1 tocando");
	lv_obj_update_layout(ui.library);
	zassert_equal(line, lv_obj_get_height(ui.mini_name), "nome do mini-player quebrou");
	zassert_false(hidden(ui.mini_time), "o nome empurrou o tempo para fora");

	toggle();
	draw();
	fake_display_dump("nomes longos, reproducao");
	lv_obj_update_layout(ui.playback);
	zassert_equal(line, lv_obj_get_height(ui.name), "nome da reproducao quebrou");
}

ZTEST(display, test_mini_player_mostra_a_faixa_sem_sair_da_lista)
{
	struct ui ui = start_ui("tres");

	/* Sem faixa: só o convite, sem ícone nem tempo. */
	zassert_str_equal("Escolha a faixa", lv_label_get_text(ui.mini_name));
	zassert_true(hidden(ui.mini_icon));
	zassert_true(hidden(ui.mini_time));
	zassert_equal(0, lv_bar_get_value(ui.mini_bar));

	publish(PLAYER_PLAYING, 1, now_us(), 200U * USEC_PER_MSEC);
	refresh();
	zassert_equal(ui.library, lv_screen_active(), "o mini-player nao troca de tela");
	zassert_false(hidden(ui.mini_icon));
	zassert_false(hidden(ui.mini_time));
	zassert_str_equal("b", lv_label_get_text(ui.mini_name));
	zassert_str_equal("0:00", lv_label_get_text(ui.mini_time));
	zassert_equal(0x60, icon_row(ui.mini_icon, 1), "esperava o icone de tocar");
	zassert_within(lv_bar_get_value(ui.mini_bar), 550, 50);

	/* A barra anda pelo relógio, e o ícone, igual, não é trocado. */
	refresh();
	zassert_true(lv_bar_get_value(ui.mini_bar) > 900);
	zassert_equal(0x60, icon_row(ui.mini_icon, 1));

	publish(PLAYER_PAUSED, 1, 62U * USEC_PER_SEC, 125U * USEC_PER_SEC);
	refresh();
	zassert_str_equal("1:02", lv_label_get_text(ui.mini_time));
	zassert_equal(0x6c, icon_row(ui.mini_icon, 1), "esperava o icone de pausa");
	zassert_equal(496, lv_bar_get_value(ui.mini_bar));

	publish(PLAYER_STOPPED, 1, 125U * USEC_PER_SEC, 125U * USEC_PER_SEC);
	refresh();
	zassert_str_equal("2:05", lv_label_get_text(ui.mini_time));
	zassert_equal(0x7c, icon_row(ui.mini_icon, 1), "esperava o icone de parado");
	zassert_equal(1000, lv_bar_get_value(ui.mini_bar));

	/* Carga recusada: o motivo é da tela de reprodução; aqui, só o aviso. */
	publish_error(2, -EINVAL, 3U, 9U);
	refresh();
	zassert_str_equal("c", lv_label_get_text(ui.mini_name));
	zassert_str_equal("erro", lv_label_get_text(ui.mini_time));
	zassert_equal(0x7c, icon_row(ui.mini_icon, 1));
	zassert_equal(0, lv_bar_get_value(ui.mini_bar));

	publish(PLAYER_STOPPED, TRACK_NONE, 0U, 0U);
	refresh();
	zassert_str_equal("Escolha a faixa", lv_label_get_text(ui.mini_name));
	zassert_true(hidden(ui.mini_icon));
	zassert_true(hidden(ui.mini_time));
	zassert_equal(0, lv_bar_get_value(ui.mini_bar));
}

/* -------------------------------------------------------------------------- */
/* Reprodução                                                                 */
/* -------------------------------------------------------------------------- */

ZTEST(display, test_reproducao_mostra_nome_estado_tempo_e_barra)
{
	struct ui ui = start_ui("tres");

	toggle();
	zassert_equal(ui.playback, lv_screen_active());

	/*
	 * Faixa de 200 ms que começa agora: depois do período que a tela espera,
	 * ~110 ms, a barra está pouco além da metade. Base no presente, e não no
	 * passado: logo depois do boot o relógio ainda não passou de 1 s.
	 */
	publish(PLAYER_PLAYING, 1, now_us(), 200U * USEC_PER_MSEC);
	refresh();
	zassert_str_equal("b", lv_label_get_text(ui.name));
	zassert_str_equal("Tocando", lv_label_get_text(ui.state));
	zassert_str_equal("0:00 / 0:00", lv_label_get_text(ui.time));
	zassert_within(lv_bar_get_value(ui.bar), 550, 50);

	/* Sem publicação nova a barra anda sozinha, pelo relógio: o player não
	 * precisa publicar a cada nota para o progresso avançar.
	 */
	refresh();
	zassert_true(lv_bar_get_value(ui.bar) > 900, "a barra parou sem publicacao nova");

	/* Pausada: base_us guarda o decorrido, e o tempo sai em minutos. */
	publish(PLAYER_PAUSED, 1, 62U * USEC_PER_SEC, 125U * USEC_PER_SEC);
	refresh();
	zassert_str_equal("Pausado", lv_label_get_text(ui.state));
	zassert_str_equal("1:02 / 2:05", lv_label_get_text(ui.time));
	zassert_equal(496, lv_bar_get_value(ui.bar));

	/* Fim da faixa: parada, com a barra cheia. */
	publish(PLAYER_STOPPED, 1, 125U * USEC_PER_SEC, 125U * USEC_PER_SEC);
	refresh();
	zassert_str_equal("Parado", lv_label_get_text(ui.state));
	zassert_str_equal("2:05 / 2:05", lv_label_get_text(ui.time));
	zassert_equal(1000, lv_bar_get_value(ui.bar));

	/* Faixa fora da biblioteca não tem nome para mostrar. */
	publish(PLAYER_STOPPED, (int16_t)LIBRARY_MAX, 0U, 0U);
	refresh();
	zassert_str_equal("Nada tocando", lv_label_get_text(ui.name));

	publish(PLAYER_PLAYING, 2, now_us(), USEC_PER_SEC);
	refresh();
	zassert_str_equal("c", lv_label_get_text(ui.name));

	publish(PLAYER_STOPPED, TRACK_NONE, 0U, 0U);
	refresh();
	zassert_str_equal("Nada tocando", lv_label_get_text(ui.name));
	zassert_equal(0, lv_bar_get_value(ui.bar));
}

ZTEST(display, test_arquivo_recusado_mostra_o_erro_e_volta_a_biblioteca)
{
	struct ui ui = start_ui("tres");

	toggle();
	publish_error(1, -EINVAL, 3U, 9U);
	refresh();
	zassert_str_equal("b", lv_label_get_text(ui.name));
	zassert_str_equal("Arquivo invalido", lv_label_get_text(ui.state));
	zassert_str_equal("linha 3 col 9", lv_label_get_text(ui.time));
	zassert_str_equal("  a\n! b\n  c", list_text(&ui));

	/* Ainda na reprodução até o prazo, sem agendar a volta duas vezes. */
	refresh();
	zassert_equal(ui.playback, lv_screen_active());

	k_msleep(DISPLAY_ERROR_MS);
	lv_timer_handler();
	zassert_equal(ui.library, lv_screen_active());

	/* Frequência fora da faixa também é recusa do parser. Na biblioteca o
	 * erro não agenda volta nenhuma: já se está nela.
	 */
	publish_error(0, -ERANGE, 0U, 0U);
	refresh();
	zassert_str_equal("! a\n! b\n  c", list_text(&ui));
	zassert_equal(ui.library, lv_screen_active());

	/* O `!` fica, e vence o `>` da faixa que volta a ser pedida. */
	publish(PLAYER_PLAYING, 1, now_us(), USEC_PER_SEC);
	refresh();
	zassert_str_equal("! a\n! b\n  c", list_text(&ui));

	/* Uma varredura nova esquece as recusas: a faixa 1, que segue no
	 * instantâneo, volta a levar `>` em vez de `!`.
	 */
	ui = start_ui("tres");
	zassert_str_equal("  a\n> b\n  c", list_text(&ui));
}

ZTEST(display, test_erro_do_cartao_nao_marca_a_faixa)
{
	struct ui ui = start_ui("tres");

	toggle();
	publish_error(0, -ENOENT, 0U, 0U);
	refresh();
	zassert_str_equal(storage_error_text(-ENOENT), lv_label_get_text(ui.state));
	zassert_str_equal("", lv_label_get_text(ui.time));
	zassert_str_equal("> a\n  b\n  c", list_text(&ui));
}

ZTEST(display, test_volta_do_erro_depois_de_sair_da_reproducao)
{
	struct ui ui = start_ui("tres");

	toggle();
	publish_error(2, -EINVAL, 2U, 1U);
	refresh();

	/* Saiu antes do prazo: quando ele vence, a biblioteca já está à mostra. */
	toggle();
	zassert_equal(ui.library, lv_screen_active());
	k_msleep(DISPLAY_ERROR_MS);
	lv_timer_handler();
	zassert_equal(ui.library, lv_screen_active());
}

/* -------------------------------------------------------------------------- */
/* Navegação entre telas                                                      */
/* -------------------------------------------------------------------------- */

ZTEST(display, test_botao_alterna_as_telas)
{
	struct ui ui = start_ui("tres");

	toggle();
	zassert_equal(ui.playback, lv_screen_active());
	toggle();
	zassert_equal(ui.library, lv_screen_active());

	/* Soltar, outro botão e o giro do encoder não alternam. */
	zassert_ok(input_report_key(NULL, DISPLAY_TOGGLE_BUTTON, 0, true, K_FOREVER));
	zassert_ok(input_report_key(NULL, INPUT_BTN_0, 1, true, K_FOREVER));
	zassert_ok(input_report_key(NULL, INPUT_BTN_0, 0, true, K_FOREVER));
	zassert_ok(input_report_rel(NULL, INPUT_REL_WHEEL, 1, true, K_FOREVER));
	refresh();
	zassert_equal(ui.library, lv_screen_active());
}

ZTEST(display, test_encoder_na_reproducao_volta_a_biblioteca)
{
	struct ui ui = start_ui("tres");
	const uint16_t keys[] = {INPUT_KEY_RIGHT, INPUT_KEY_LEFT, INPUT_KEY_ENTER};

	ARRAY_FOR_EACH(keys, i) {
		toggle();
		zassert_equal(ui.playback, lv_screen_active(), "tecla %zu", i);

		tap(keys[i]);
		zassert_equal(ui.library, lv_screen_active(), "tecla %zu", i);

		/* O giro e o clique que voltam não chegam à lista: o foco não
		 * anda, e nenhuma faixa é pedida.
		 */
		zassert_equal(0, focused(&ui), "tecla %zu", i);
		expect_no_cmd();
	}
}

ZTEST(display, test_reiniciar_descarta_as_telas_anteriores)
{
	struct ui ui = start_ui("tres");

	/* Na reprodução, com a volta do erro agendada. */
	toggle();
	publish_error(1, -EINVAL, 2U, 1U);
	refresh();
	zassert_equal(ui.playback, lv_screen_active());

	struct ui fresh = start_ui("tres");

	zassert_not_equal(ui.library, fresh.library);
	zassert_equal(fresh.library, lv_screen_active());

	/* A volta agendada foi apagada junto com as telas antigas: se não
	 * fosse, dispararia sobre objetos já liberados.
	 */
	k_msleep(DISPLAY_ERROR_MS);
	lv_timer_handler();
	zassert_equal(fresh.library, lv_screen_active());
}

/* -------------------------------------------------------------------------- */
/* Blanking                                                                   */
/* -------------------------------------------------------------------------- */

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

/**
 * @file display.c
 * @brief Interface no OLED: tela de biblioteca e tela de reprodução.
 *
 * Duas telas, como a especificação pede:
 *
 * - **Biblioteca**: a lista inteira no roller. O destaque é o foco, que só o
 *   encoder move; a faixa do instantâneo leva `>`, e a recusada leva `!`.
 *   Foco e marcador são coisas diferentes de propósito: trocar de faixa pelos
 *   botões não mexe no foco de quem está navegando.
 * - **Reprodução**: nome, estado, tempo decorrido e total, e a barra.
 *
 * Clicar numa faixa a toca e abre a reprodução. Na reprodução, girar ou
 * clicar o encoder volta à biblioteca; o botão ::DISPLAY_TOGGLE_BUTTON
 * alterna entre as duas. O transporte não depende da tela: os botões de
 * tocar, anterior e seguinte valem nas duas.
 *
 * A lista sai de @ref storage_scan e o resto, de @ref snapshot_read. Nada
 * aqui sabe quem responde ao @ref storage.h — cartão na placa, faixas
 * embutidas ou pasta do computador no simulador — nem se há um player de
 * verdade do outro lado do instantâneo.
 */

#include <vitrolinha/cmd.h>
#include <vitrolinha/display.h>
#include <vitrolinha/snapshot.h>
#include <vitrolinha/storage.h>
#include <vitrolinha/track.h>

#include <lvgl.h>
#include <lvgl_input_device.h>
#include <lvgl_zephyr.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(display, LOG_LEVEL_INF);

/**
 * @brief Habilita o teste de desempenho do display.
 *
 * Quando definido, substitui a interface por uma tela de demonstração que
 * invalida a tela inteira periodicamente e registra a taxa de flushes.
 */
/* #define TEST_FPS */

/**
 * @brief Dispositivo de display definido pelo nó chosen do Devicetree.
 */
static const struct device *const display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

#ifndef TEST_FPS

/**
 * @brief Nó do Devicetree do dispositivo de entrada que dirige os grupos.
 *
 * Escolhido pelo que o devicetree declara, não pelo nome da placa:
 * - no simulador, o overlay declara um keypad que entrega as setas e o Enter
 *   do teclado do computador;
 * - na placa, o `lvgl_encoder_input` do app.overlay, que recebe o
 *   `INPUT_REL_WHEEL` do gpio-qdec e o clique do encoder.
 */
#if DT_NODE_EXISTS(DT_NODELABEL(lvgl_keypad))
#define INPUT_NODE DT_NODELABEL(lvgl_keypad)
#else
#define INPUT_NODE DT_NODELABEL(lvgl_encoder_input)
#endif

static const struct device *const input_dev = DEVICE_DT_GET(INPUT_NODE);

/** @brief Escala da barra: a mesma de @ref snapshot_progress_permille. */
#define BAR_MAX 1000

/** @brief Linhas visíveis da biblioteca. Ímpar, para o foco ficar no meio. */
#define LIBRARY_ROWS 5

/** @brief Marcador e espaço antes de cada nome da lista. */
#define MARKER_LEN 2U

/**
 * @brief Tamanho das opções do roller.
 *
 * Cada faixa ocupa o marcador, o nome (até TRACK_NAME_MAX - 1 bytes) e o
 * `\n`. Mais o terminador.
 */
#define ROLLER_OPTIONS_SIZE (LIBRARY_MAX * (MARKER_LEN + TRACK_NAME_MAX) + 1U)

/** @brief Texto do rótulo de nome quando o instantâneo não tem faixa. */
#define NOTHING_PLAYING "Nada tocando"

/* -------------------------------------------------------------------------- */
/* Estado                                                                     */
/* -------------------------------------------------------------------------- */

/** @brief Biblioteca, na ordem de @ref storage_scan. Tamanho fixo. */
static struct track_meta library[LIBRARY_MAX];

/** @brief Quantas posições de @ref library valem. Zero se a varredura falhou. */
static size_t library_count;

/** @brief Erro da varredura, ou 0. Decide o texto do roller vazio. */
static int library_error;

/**
 * @brief Faixas marcadas com `!`: as que a varredura já sabe inválidas e as
 *        que o parser recusou na carga desde a última varredura.
 */
static bool rejected[LIBRARY_MAX];

/** @brief Faixa que leva `>` nas opções do roller agora. */
static int16_t marked_track = TRACK_NONE;

/** @brief Opções do roller, uma faixa por linha. O LVGL copia; o buffer é rascunho. */
static char options[ROLLER_OPTIONS_SIZE];

/** @brief Pedido de alternar a tela, vindo do botão; atendido no timer do LVGL. */
static atomic_t toggle_requested;

static lv_indev_t *indev;

static lv_obj_t *library_screen;
static lv_obj_t *roller;
static lv_group_t *library_group;

static lv_obj_t *playback_screen;
static lv_obj_t *name_label;
static lv_obj_t *state_label;
static lv_obj_t *time_label;
static lv_obj_t *bar;
static lv_group_t *playback_group;

static lv_timer_t *refresh_timer;

/** @brief Volta da tela de erro à biblioteca, pendente; NULL se nenhuma. */
static lv_timer_t *error_timer;

/* -------------------------------------------------------------------------- */
/* Biblioteca                                                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Varre a biblioteca pelo contrato do @ref storage.h.
 *
 * Fora do lock do LVGL: na placa a varredura lê o cartão, e o laço do LVGL
 * não tem por que esperar por ela.
 */
static void library_load(void)
{
	size_t skipped = 0;
	int count = storage_scan(library, ARRAY_SIZE(library), &skipped);

	memset(rejected, 0, sizeof(rejected));

	if (count < 0) {
		LOG_WRN("Biblioteca indisponivel: %s (%d)", storage_error_text(count), count);
		library_count = 0;
		library_error = count;
		return;
	}

	library_count = (size_t)count;
	library_error = 0;

	/* O que a varredura já sabe inválido (cabeçalho errado, maior que o
	 * buffer) nasce recusado; o que o parser recusar se junta na carga.
	 */
	for (size_t i = 0; i < library_count; i++) {
		rejected[i] = !library[i].valid;
	}
}

static bool track_listed(int track)
{
	return (track >= 0) && ((size_t)track < library_count);
}

/**
 * @brief Erro de carga que é do conteúdo do arquivo, e não do cartão.
 *
 * O parser só devolve estes dois; os do armazenamento são outros (ver
 * @ref storage_error_text). É o que distingue "arquivo inválido", que marca
 * a faixa com `!`, de "sem cartão", que não diz nada sobre ela.
 */
static bool is_parser_error(int err)
{
	return (err == -EINVAL) || (err == -ERANGE);
}

/**
 * @brief Marcador de uma linha da lista.
 *
 * `!` para a faixa recusada (ver @ref rejected), `>` para a faixa do
 * instantâneo. O `!` vence: a faixa recusada não está tocando.
 */
static char track_marker(size_t i, int16_t playing)
{
	if (rejected[i]) {
		return '!';
	}

	return ((int)i == playing) ? '>' : ' ';
}

/**
 * @brief Monta as opções do roller em @ref options, uma faixa por linha.
 *
 * Biblioteca vazia vira uma única linha explicando o porquê.
 */
static void library_options(int16_t playing)
{
	size_t used = 0;

	if (library_count == 0U) {
		const char *why = (library_error != 0) ? storage_error_text(library_error)
						       : "Biblioteca vazia";

		strncpy(options, why, sizeof(options) - 1U);
		options[sizeof(options) - 1U] = '\0';
		return;
	}

	for (size_t i = 0; i < library_count; i++) {
		/* track_meta_init garante o terminador dentro de TRACK_NAME_MAX. */
		size_t len = strlen(library[i].name);

		if (i > 0U) {
			options[used++] = '\n';
		}

		options[used++] = track_marker(i, playing);
		options[used++] = ' ';
		memcpy(&options[used], library[i].name, len);
		used += len;
	}

	options[used] = '\0';
}

/**
 * @brief Reescreve os marcadores da lista se a faixa ou as recusas mudaram.
 *
 * O foco fica onde estava: `lv_roller_set_options` o devolve ao topo, e ele
 * é reposto em seguida.
 */
static void library_mark(const struct player_snapshot *snap)
{
	bool dirty = (snap->track != marked_track);

	if (track_listed(snap->track) && is_parser_error(snap->load_error) &&
	    !rejected[snap->track]) {
		rejected[snap->track] = true;
		dirty = true;
	}

	if (!dirty) {
		return;
	}

	marked_track = snap->track;

	if (library_count == 0U) {
		return;
	}

	uint32_t focused = lv_roller_get_selected(roller);

	library_options(marked_track);
	lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
	lv_roller_set_selected(roller, focused, LV_ANIM_OFF);
}

/* -------------------------------------------------------------------------- */
/* Navegação entre telas                                                      */
/* -------------------------------------------------------------------------- */

static void show(lv_obj_t *screen, lv_group_t *group)
{
	if (lv_screen_active() != screen) {
		lv_screen_load(screen);
	}

	/* O encoder fala com o grupo da tela à mostra, e só com ele: sem isto
	 * girar na reprodução moveria o foco de uma lista que não está na tela.
	 */
	lv_indev_set_group(indev, group);
}

static void show_library(void)
{
	show(library_screen, library_group);
}

static void show_playback(void)
{
	show(playback_screen, playback_group);
}

static void error_timer_cb(lv_timer_t *timer)
{
	ARG_UNUSED(timer);

	/* Único disparo: o LVGL apaga o timer ao voltar daqui. */
	error_timer = NULL;
	show_library();
}

/**
 * @brief Recebe todo evento de entrada e fica só com o botão de alternar.
 *
 * Roda no contexto do subsistema `input`, fora do lock do LVGL: só levanta
 * o pedido, e quem troca a tela é o timer de atualização.
 */
static void toggle_button_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if ((evt->type == INPUT_EV_KEY) && (evt->code == DISPLAY_TOGGLE_BUTTON) &&
	    (evt->value != 0)) {
		atomic_set(&toggle_requested, 1);
	}
}

INPUT_CALLBACK_DEFINE(NULL, toggle_button_cb, NULL);

/* -------------------------------------------------------------------------- */
/* Reprodução                                                                 */
/* -------------------------------------------------------------------------- */

/** @brief Só reescreve o rótulo quando o texto muda, para não redesenhar à toa. */
static void label_set(lv_obj_t *label, const char *text)
{
	if (strcmp(lv_label_get_text(label), text) != 0) {
		lv_label_set_text(label, text);
	}
}

static const char *state_text(const struct player_snapshot *snap)
{
	if (snap->load_error != 0) {
		return is_parser_error(snap->load_error) ? "Arquivo invalido"
							 : storage_error_text(snap->load_error);
	}

	switch (snap->state) {
	case PLAYER_PLAYING:
		return "Tocando";
	case PLAYER_PAUSED:
		return "Pausado";
	default:
		return "Parado";
	}
}

/** @brief Copia o instantâneo para os rótulos e a barra da reprodução. */
static void playback_update(const struct player_snapshot *snap, uint64_t now_us)
{
	char text[32];

	label_set(name_label,
		  track_listed(snap->track) ? library[snap->track].name : NOTHING_PLAYING);
	label_set(state_label, state_text(snap));

	if (snap->load_error != 0) {
		/* No lugar do tempo, onde o arquivo quebrou, quando se sabe. */
		if (snap->error_line != 0U) {
			snprintf(text, sizeof(text), "linha %u", snap->error_line);
		} else {
			text[0] = '\0';
		}
	} else {
		uint32_t elapsed_s = snapshot_elapsed_us(snap, now_us) / USEC_PER_SEC;
		uint32_t total_s = snap->total_us / USEC_PER_SEC;

		snprintf(text, sizeof(text), "%u:%02u / %u:%02u", elapsed_s / 60U,
			 elapsed_s % 60U, total_s / 60U, total_s % 60U);
	}
	label_set(time_label, text);

	lv_bar_set_value(bar, snapshot_progress_permille(snap, now_us), LV_ANIM_OFF);
}

/**
 * @brief Copia o instantâneo para as duas telas e atende o botão de alternar.
 *
 * Roda a taxa fixa, pelo timer do LVGL — com o lock do LVGL já adquirido —,
 * e nunca é acordada pelo player.
 */
static void refresh_timer_cb(lv_timer_t *timer)
{
	ARG_UNUSED(timer);

	struct player_snapshot snap;
	uint64_t now_us = k_ticks_to_us_floor64(k_uptime_ticks());

	snapshot_read(&snap);

	/* Rastro de bancada: cada publicação nova do player sai no console. Daqui,
	 * e não do player, porque é a thread de baixa prioridade que já lê o
	 * instantâneo. O decorrido não mora no instantâneo — sai de base_us e do
	 * relógio —, então o registro só muda quando o player publica algo novo.
	 */
	static struct player_snapshot logged = SNAPSHOT_IDLE_INITIALIZER;

	if (memcmp(&snap, &logged, sizeof(snap)) != 0) {
		LOG_INF("Player: estado %d, faixa %d, %u/%u ms, erro %d (linha %u)", snap.state,
			snap.track, snapshot_elapsed_us(&snap, now_us) / 1000U,
			snap.total_us / 1000U, snap.load_error, snap.error_line);
		logged = snap;
	}

	if (atomic_cas(&toggle_requested, 1, 0)) {
		if (lv_screen_active() == playback_screen) {
			show_library();
		} else {
			show_playback();
		}
	}

	playback_update(&snap, now_us);
	library_mark(&snap);

	/* A tela de erro é a própria reprodução mostrando o motivo; depois de
	 * um tempo ela devolve à biblioteca, onde a faixa já aparece com `!`.
	 */
	if ((snap.load_error != 0) && (lv_screen_active() == playback_screen) &&
	    (error_timer == NULL)) {
		error_timer = lv_timer_create(error_timer_cb, DISPLAY_ERROR_MS, NULL);
		lv_timer_set_repeat_count(error_timer, 1);
	}
}

/* -------------------------------------------------------------------------- */
/* Eventos de interação                                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Clique no roller: toca a faixa em foco e abre a reprodução.
 *
 * Publica ::CMD_SELECT; o que a reprodução mostra é o que o player publicar
 * no instantâneo em resposta — a tela mostra o que está tocando, não o que
 * foi pedido.
 *
 * Reabre o modo de edição do grupo: com o encoder, o clique o encerra, e sem
 * ele o giro passaria a mover o foco em vez da seleção do roller.
 */
static void roller_clicked_cb(lv_event_t *e)
{
	lv_obj_t *target = lv_event_get_target_obj(e);
	lv_group_t *g = lv_obj_get_group(target);
	uint32_t selected = lv_roller_get_selected(target);
	int err;

	if (g != NULL) {
		lv_group_set_editing(g, true);
	}

	if (selected >= library_count) {
		return;
	}

	err = cmd_post(CMD_SELECT, (int16_t)library[selected].index);
	if (err != 0) {
		LOG_WRN("Selecao da faixa %u descartada: %d", library[selected].index, err);
		return;
	}

	show_playback();
}

/**
 * @brief Encoder na reprodução: girar ou clicar volta à biblioteca.
 *
 * Só as setas contam como giro. O keypad do simulador manda o Enter como
 * tecla já ao apertar, e o clique só ao soltar: voltar no Enter apertado
 * entregaria o soltar à lista, que tocaria a faixa em foco.
 */
static void playback_input_cb(lv_event_t *e)
{
	if (lv_event_get_code(e) == LV_EVENT_KEY) {
		uint32_t key = lv_event_get_key(e);

		if ((key != LV_KEY_LEFT) && (key != LV_KEY_RIGHT)) {
			return;
		}
	}

	show_library();
}

/* -------------------------------------------------------------------------- */
/* Construção das telas                                                       */
/* -------------------------------------------------------------------------- */

/** @brief Tela vazia, sem rolagem: as duas telas cabem inteiras em 128x64. */
static lv_obj_t *screen_create(void)
{
	lv_obj_t *screen = lv_obj_create(NULL);

	lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
	lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

	return screen;
}

static void library_build(void)
{
	static lv_style_t style_main;
	static lv_style_t style_selected;
	static bool styled;

	if (!styled) {
		lv_style_init(&style_main);
		lv_style_set_bg_color(&style_main, lv_color_hex(0xffffff));
		lv_style_set_border_opa(&style_main, 0);
		lv_style_set_radius(&style_main, 0);
		lv_style_set_text_color(&style_main, lv_color_hex(0x000000));
		lv_style_set_text_line_space(&style_main, 2);
		/* À esquerda, para os marcadores formarem uma coluna. */
		lv_style_set_text_align(&style_main, LV_TEXT_ALIGN_LEFT);

		lv_style_init(&style_selected);
		lv_style_set_bg_color(&style_selected, lv_color_hex(0x000000));
		lv_style_set_text_color(&style_selected, lv_color_hex(0xffffff));
		lv_style_set_text_align(&style_selected, LV_TEXT_ALIGN_LEFT);

		styled = true;
	}

	library_screen = screen_create();

	roller = lv_roller_create(library_screen);
	lv_obj_add_style(roller, &style_main, LV_PART_MAIN);
	lv_obj_add_style(roller, &style_selected, LV_PART_SELECTED);
	lv_obj_set_width(roller, LV_HOR_RES);
	lv_roller_set_visible_row_count(roller, LIBRARY_ROWS);
	lv_obj_center(roller);

	marked_track = TRACK_NONE;
	library_options(marked_track);
	lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
	lv_roller_set_selected(roller, 0, LV_ANIM_OFF);
	lv_obj_add_event_cb(roller, roller_clicked_cb, LV_EVENT_CLICKED, NULL);

	library_group = lv_group_create();
	lv_group_add_obj(library_group, roller);
	lv_group_set_editing(library_group, true);
}

static lv_obj_t *playback_label(void)
{
	lv_obj_t *label = lv_label_create(playback_screen);

	/* Largura fixa e reticências: um nome comprido não quebra em duas linhas
	 * nem empurra o resto da tela.
	 */
	lv_obj_set_width(label, LV_HOR_RES);
	lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
	lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(label, "");

	return label;
}

static void playback_build(void)
{
	static lv_style_t style_bar;
	static bool styled;

	if (!styled) {
		lv_style_init(&style_bar);
		lv_style_set_radius(&style_bar, 12);
		styled = true;
	}

	playback_screen = screen_create();
	lv_obj_set_flex_flow(playback_screen, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(playback_screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_row(playback_screen, 4, 0);

	/* A tela é o alvo do encoder aqui, e o foco não deve desenhar contorno. */
	lv_obj_set_style_outline_width(playback_screen, 0, LV_STATE_ANY);
	lv_obj_set_style_border_width(playback_screen, 0, LV_STATE_ANY);

	name_label = playback_label();
	label_set(name_label, NOTHING_PLAYING);
	state_label = playback_label();
	time_label = playback_label();

	bar = lv_bar_create(playback_screen);
	lv_obj_set_size(bar, lv_pct(90), 8);
	lv_bar_set_range(bar, 0, BAR_MAX);
	lv_obj_add_style(bar, &style_bar, LV_PART_MAIN);

	lv_obj_add_event_cb(playback_screen, playback_input_cb, LV_EVENT_KEY, NULL);
	lv_obj_add_event_cb(playback_screen, playback_input_cb, LV_EVENT_CLICKED, NULL);

	/* Em edição, o giro do encoder chega como tecla, e não como troca de
	 * foco num grupo de um objeto só.
	 */
	playback_group = lv_group_create();
	lv_group_add_obj(playback_group, playback_screen);
	lv_group_set_editing(playback_group, true);
}

/**
 * @brief Monta as duas telas, mostra a biblioteca e descarta as anteriores.
 *
 * As anteriores são as de uma chamada anterior a @ref display_init, mais a
 * tela vazia que o LVGL cria com o display.
 */
static void ui_build(void)
{
	lv_obj_t *previous = lv_screen_active();
	lv_obj_t *old_library = library_screen;
	lv_obj_t *old_playback = playback_screen;
	lv_group_t *old_library_group = library_group;
	lv_group_t *old_playback_group = playback_group;

	if (error_timer != NULL) {
		lv_timer_delete(error_timer);
		error_timer = NULL;
	}
	if (refresh_timer != NULL) {
		lv_timer_delete(refresh_timer);
	}
	atomic_clear(&toggle_requested);

	indev = lvgl_input_get_indev(input_dev);

	library_build();
	playback_build();
	show_library();

	/* Grupos depois de o indev já apontar para o novo; telas depois de a
	 * nova estar carregada. lv_group_delete(NULL) e lv_obj_delete(NULL) não
	 * são permitidos, daí as guardas.
	 */
	if (old_library_group != NULL) {
		lv_group_delete(old_library_group);
		lv_group_delete(old_playback_group);
		lv_obj_delete(old_library);
		lv_obj_delete(old_playback);
	}
	if ((previous != old_library) && (previous != old_playback)) {
		lv_obj_delete(previous);
	}

	refresh_timer = lv_timer_create(refresh_timer_cb, DISPLAY_REFRESH_PERIOD_MS, NULL);
}

#endif /* !TEST_FPS */

/* -------------------------------------------------------------------------- */
/* Medição de desempenho                                                      */
/* -------------------------------------------------------------------------- */

#ifdef TEST_FPS

static lv_obj_t *frame_label;
static uint32_t frame_counter;
static uint32_t fps_frame_count;
static int64_t fps_window_start_ms;

/**
 * @brief Conta os flushes concluídos e registra a taxa a cada segundo.
 *
 * É flushes por segundo; vira FPS só quando cada quadro é um flush. Roda no
 * contexto do LVGL, com o lock já adquirido — não o adquirir de novo aqui.
 */
static void display_flush_event_cb(lv_event_t *e)
{
	ARG_UNUSED(e);

	int64_t now_ms = k_uptime_get();
	int64_t elapsed_ms;

	if (fps_window_start_ms == 0) {
		fps_window_start_ms = now_ms;
	}

	fps_frame_count++;
	elapsed_ms = now_ms - fps_window_start_ms;

	if (elapsed_ms >= 1000) {
		uint32_t fps_x10 = (uint32_t)((fps_frame_count * 10000) / elapsed_ms);

		LOG_INF("Taxa de quadros real: %u.%u fps (%u quadros em %u ms)", fps_x10 / 10,
			fps_x10 % 10, fps_frame_count, (uint32_t)elapsed_ms);

		fps_frame_count = 0;
		fps_window_start_ms = now_ms;
	}
}

/** @brief Atualiza o contador e invalida a tela inteira. */
static void display_demo_timer_cb(lv_timer_t *timer)
{
	ARG_UNUSED(timer);

	frame_counter++;
	lv_label_set_text_fmt(frame_label, "quadro %u", frame_counter);
	lv_obj_invalidate(lv_screen_active());
}

static void ui_build(void)
{
	lv_obj_t *title = lv_label_create(lv_screen_active());

	lv_label_set_text(title, "Vitrola");
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

	frame_label = lv_label_create(lv_screen_active());
	lv_label_set_text(frame_label, "quadro 0");
	lv_obj_align(frame_label, LV_ALIGN_BOTTOM_MID, 0, 0);

	lv_display_add_event_cb(lv_display_get_default(), display_flush_event_cb,
				LV_EVENT_FLUSH_FINISH, NULL);
	lv_timer_create(display_demo_timer_cb, DISPLAY_DEMO_PERIOD_MS, NULL);

	/* Pede o primeiro quadro antes de desligar o blanking. */
	lv_timer_handler();
}

#endif /* TEST_FPS */

/* -------------------------------------------------------------------------- */
/* Inicialização                                                              */
/* -------------------------------------------------------------------------- */

int display_init(void)
{
	int err;

	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display nao esta pronto");
		return -ENODEV;
	}

#ifndef TEST_FPS
	library_load();
#endif

	/* A função pode rodar em paralelo com o laço periódico do LVGL; o lock
	 * protege a criação e a configuração dos objetos.
	 */
	lvgl_lock();
	ui_build();
	lvgl_unlock();

	err = display_blanking_off(display_dev);
	if ((err != 0) && (err != -ENOSYS)) {
		LOG_ERR("Nao consegui desligar o blanking do display: %d", err);
		return err;
	}

	LOG_INF("Display no ar (%s)", display_dev->name);

	return 0;
}

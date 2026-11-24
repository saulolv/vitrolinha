/**
 * @file display_ui.c
 * @brief Núcleo da interface: as duas telas, a navegação entre elas e o
 *        timer que as atualiza.
 *
 * Monta a biblioteca (display_library.c, com o mini-player de
 * display_mini.c) e a reprodução (display_playback.c), troca de tela e de
 * grupo do encoder, atende o botão de alternar e copia o instantâneo para as
 * telas a taxa fixa. Também guarda o que as telas dividem: os rótulos que só
 * redesenham quando o texto muda e a barra de progresso.
 */

#include <vitrolinha/display.h>
#include <vitrolinha/library.h>
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
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "display_internal.h"

LOG_MODULE_DECLARE(display, LOG_LEVEL_INF);

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

/* -------------------------------------------------------------------------- */
/* Estado                                                                     */
/* -------------------------------------------------------------------------- */

/** @brief Pedido de alternar a tela, vindo do botão; atendido no timer do LVGL. */
static atomic_t toggle_requested;

/**
 * @brief Faixa pedida por @ref display_cue, mais um; zero se nenhuma.
 *
 * Mais um para que o zero de toda variável estática signifique "nada".
 */
static atomic_t cue_requested;

static lv_indev_t *indev;

static lv_obj_t *library_screen;
static lv_group_t *library_group;

static lv_obj_t *playback_screen;
static lv_group_t *playback_group;

static lv_timer_t *refresh_timer;

/** @brief Volta da tela de erro à biblioteca, pendente; NULL se nenhuma. */
static lv_timer_t *error_timer;

/* -------------------------------------------------------------------------- */
/* Rótulos e barra, comuns às duas telas                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Maior texto de rótulo, com o terminador: "> " e um nome de 23
 *        bytes, ou "linha 4096 col 4096".
 */
#define LABEL_TEXT_SIZE 32U

/** @brief Rótulos que @ref ui_label_set atualiza: as linhas, 2 no rodapé, 3 na reprodução. */
#define LABEL_COUNT (LIBRARY_ROWS + 2U + 3U)

BUILD_ASSERT(LABEL_TEXT_SIZE > (2U + TRACK_NAME_MAX), "nome com marcador não cabe na cópia");

/** @brief Cópia do texto pedido de cada rótulo; ver @ref ui_label_create. */
static char label_shadows[LABEL_COUNT][LABEL_TEXT_SIZE];

/** @brief Cópias já entregues na montagem corrente das telas. */
static size_t label_shadows_used;

/**
 * @brief Cria um rótulo cujo texto @ref ui_label_set compara antes de reescrever.
 *
 * O texto pedido fica numa cópia própria, apontada pelo user_data, e não é
 * relido do rótulo: com reticências o LVGL reescreve o texto guardado,
 * trocando o fim por `...`, e a comparação nunca bateria com um nome longo.
 */
lv_obj_t *ui_label_create(lv_obj_t *parent)
{
	lv_obj_t *label = lv_label_create(parent);
	char *shadow = label_shadows[label_shadows_used++];

	shadow[0] = '\0';
	lv_obj_set_user_data(label, shadow);
	lv_label_set_text(label, "");
	return label;
}

void ui_label_set(lv_obj_t *label, const char *text)
{
	char *shadow = lv_obj_get_user_data(label);

	if (strcmp(shadow, text) != 0) {
		strncpy(shadow, text, LABEL_TEXT_SIZE - 1U);
		lv_label_set_text(label, text);
	}
}

/**
 * @brief Barra de progresso no visual das duas telas.
 *
 * O do tema mono, com as pontas arredondadas e 90% da largura, a mesma nas
 * duas telas. Um contorno quadrado na largura toda ficou pesado no OLED.
 */
lv_obj_t *ui_progress_bar_create(lv_obj_t *parent, int32_t height)
{
	static lv_style_t style_bar;
	static bool styled;

	if (!styled) {
		lv_style_init(&style_bar);
		lv_style_set_radius(&style_bar, 12);
		styled = true;
	}

	lv_obj_t *created = lv_bar_create(parent);

	lv_obj_add_style(created, &style_bar, LV_PART_MAIN);
	lv_obj_set_size(created, lv_pct(90), height);
	lv_bar_set_range(created, 0, BAR_MAX);

	return created;
}

void ui_format_mmss(char out[MMSS_SIZE], uint32_t us)
{
	uint32_t s = us / USEC_PER_SEC;

	snprintf(out, MMSS_SIZE, "%u:%02u", s / 60U, s % 60U);
}

/**
 * @brief Erro de carga que é do conteúdo do arquivo, e não do cartão.
 *
 * O parser só devolve estes dois; os do armazenamento são outros (ver
 * @ref storage_error_text). É o que distingue "arquivo inválido", que marca
 * a faixa com `!`, de "sem cartão", que não diz nada sobre ela.
 */
bool ui_is_parser_error(int err)
{
	return (err == -EINVAL) || (err == -ERANGE);
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

void ui_show_library(void)
{
	show(library_screen, library_group);
}

void ui_show_playback(void)
{
	show(playback_screen, playback_group);
}

static void error_timer_cb(lv_timer_t *timer)
{
	ARG_UNUSED(timer);

	/* Único disparo: o LVGL apaga o timer ao voltar daqui. */
	error_timer = NULL;
	ui_show_library();
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

void display_cue(int track)
{
	atomic_set(&cue_requested, (atomic_val_t)track + 1);
}

/* -------------------------------------------------------------------------- */
/* Atualização                                                                */
/* -------------------------------------------------------------------------- */

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
		LOG_INF("Player: estado %d, faixa %d, %u/%u ms, erro %d (linha %u col %u)",
			snap.state, snap.track, snapshot_elapsed_us(&snap, now_us) / 1000U,
			snap.total_us / 1000U, snap.load_error, snap.error_line,
			snap.error_column);
		logged = snap;
	}

	if (atomic_cas(&toggle_requested, 1, 0)) {
		if (lv_screen_active() == playback_screen) {
			ui_show_library();
		} else {
			ui_show_playback();
		}
	}

	ui_library_refresh();

	/* Depois da lista: a faixa lembrada só existe na lista já copiada. Um
	 * índice que não está nela é descartado, sem mover nada.
	 */
	ui_library_cue((int)atomic_set(&cue_requested, 0) - 1);

	ui_playback_update(&snap, now_us);
	ui_mini_update(&snap, now_us);
	ui_library_mark(&snap);

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

void ui_prepare(void)
{
	(void)ui_library_sync();
}

/**
 * @brief Monta as duas telas, mostra a biblioteca e descarta as anteriores.
 *
 * As anteriores são as de uma chamada anterior a @ref display_init, mais a
 * tela vazia que o LVGL cria com o display.
 */
void ui_build(void)
{
	lv_obj_t *previous = lv_screen_active();

	/* As telas antigas só são apagadas no fim, mas nada mais escreve nelas:
	 * as cópias de texto podem ser reaproveitadas pelas novas.
	 */
	label_shadows_used = 0U;
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

	library_screen = screen_create();
	library_group = ui_library_build(library_screen);
	playback_screen = screen_create();
	playback_group = ui_playback_build(playback_screen);
	ui_show_library();

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

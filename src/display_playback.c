/**
 * @file display_playback.c
 * @brief A tela de reprodução: nome, estado, tempo decorrido e total, a
 *        barra e o motivo de uma carga recusada.
 *
 * Girar ou clicar o encoder aqui volta à biblioteca.
 */

#include <vitrolinha/snapshot.h>
#include <vitrolinha/storage.h>

#include <lvgl.h>

#include <stdio.h>

#include "display_internal.h"

/** @brief Texto do rótulo de nome quando o instantâneo não tem faixa. */
#define NOTHING_PLAYING "Nada tocando"

static lv_obj_t *name_label;
static lv_obj_t *state_label;
static lv_obj_t *time_label;
static lv_obj_t *bar;

/* -------------------------------------------------------------------------- */
/* Atualização                                                                */
/* -------------------------------------------------------------------------- */

static const char *state_text(const struct player_snapshot *snap)
{
	if (snap->load_error != 0) {
		return ui_is_parser_error(snap->load_error) ? "Arquivo invalido"
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

void ui_playback_update(const struct player_snapshot *snap, uint64_t now_us)
{
	char text[2 * MMSS_SIZE + 3];
	const char *name = ui_track_name(snap->track);

	ui_label_set(name_label, (name != NULL) ? name : NOTHING_PLAYING);
	ui_label_set(state_label, state_text(snap));

	if (snap->load_error != 0) {
		/* No lugar do tempo, onde o arquivo quebrou, quando se sabe. A
		 * coluna é o que localiza o erro num RTTTL, de uma linha só.
		 */
		if (snap->error_line != 0U) {
			snprintf(text, sizeof(text), "linha %u col %u", snap->error_line,
				 snap->error_column);
		} else {
			text[0] = '\0';
		}
	} else {
		char elapsed[MMSS_SIZE];
		char total[MMSS_SIZE];

		ui_format_mmss(elapsed, snapshot_elapsed_us(snap, now_us));
		ui_format_mmss(total, snap->total_us);
		snprintf(text, sizeof(text), "%s / %s", elapsed, total);
	}
	ui_label_set(time_label, text);

	lv_bar_set_value(bar, snapshot_progress_permille(snap, now_us), LV_ANIM_OFF);
}

/* -------------------------------------------------------------------------- */
/* Eventos de interação                                                       */
/* -------------------------------------------------------------------------- */

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

	ui_show_library();
}

/* -------------------------------------------------------------------------- */
/* Construção                                                                 */
/* -------------------------------------------------------------------------- */

static lv_obj_t *playback_label(lv_obj_t *screen)
{
	lv_obj_t *label = ui_label_create(screen);

	/* Tamanho fixo e reticências: um nome comprido não quebra em duas
	 * linhas nem empurra o resto da tela. Sem a altura fixa, as reticências
	 * não valem.
	 */
	lv_obj_set_size(label, LV_HOR_RES, ROW_HEIGHT);
	lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
	lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

	return label;
}

lv_group_t *ui_playback_build(lv_obj_t *screen)
{
	lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_row(screen, 4, 0);

	/* A tela é o alvo do encoder aqui, e o foco não deve desenhar contorno. */
	lv_obj_set_style_outline_width(screen, 0, LV_STATE_ANY);
	lv_obj_set_style_border_width(screen, 0, LV_STATE_ANY);

	name_label = playback_label(screen);
	ui_label_set(name_label, NOTHING_PLAYING);
	state_label = playback_label(screen);
	time_label = playback_label(screen);

	bar = ui_progress_bar_create(screen, PLAYBACK_BAR_HEIGHT);

	lv_obj_add_event_cb(screen, playback_input_cb, LV_EVENT_KEY, NULL);
	lv_obj_add_event_cb(screen, playback_input_cb, LV_EVENT_CLICKED, NULL);

	/* Em edição, o giro do encoder chega como tecla, e não como troca de
	 * foco num grupo de um objeto só.
	 */
	lv_group_t *group = lv_group_create();

	lv_group_add_obj(group, screen);
	lv_group_set_editing(group, true);

	return group;
}

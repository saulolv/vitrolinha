/**
 * @file display_mini.c
 * @brief O mini-player no rodapé da tela de biblioteca.
 *
 * Mostra o que toca sem sair da lista: ícone de estado, nome, decorrido e a
 * barra. O total e o motivo de um erro ficam para a tela de reprodução, que
 * tem espaço para eles.
 */

#include <vitrolinha/snapshot.h>

#include <lvgl.h>

#include "display_internal.h"

/** @brief O mini-player sem faixa: cabe nas 16 colunas, e diz o que fazer. */
#define CHOOSE_TRACK "Escolha a faixa"

/* -------------------------------------------------------------------------- */
/* Ícones                                                                     */
/* -------------------------------------------------------------------------- */

/*
 * Ícones de estado, 7x7, em 1 bit indexado. A UNSCII-8 só tem ASCII, sem os
 * símbolos de tocar e pausar, e desenhar à mão custa quinze bytes por ícone.
 *
 * A paleta vem antes dos pixels, em lv_color32_t (azul, verde, vermelho,
 * alfa): índice 0 transparente, índice 1 na cor do texto. Cada linha é um
 * byte, o pixel da esquerda no bit mais alto.
 */
#define ICON_SIZE    7
#define ICON_PALETTE 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff

static const uint8_t icon_play_map[] = {
	ICON_PALETTE,
	0x40, /* .X..... */
	0x60, /* .XX.... */
	0x70, /* .XXX... */
	0x78, /* .XXXX.. */
	0x70, /* .XXX... */
	0x60, /* .XX.... */
	0x40, /* .X..... */
};

static const uint8_t icon_pause_map[] = {
	ICON_PALETTE, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c, /* .XX.XX. */
};

static const uint8_t icon_stop_map[] = {
	ICON_PALETTE, 0x00, 0x7c, 0x7c, 0x7c, 0x7c, 0x7c, 0x00, /* .XXXXX. */
};

#define ICON_DSC(map)                                                                      \
	{                                                                                  \
		.header = {.magic = LV_IMAGE_HEADER_MAGIC,                                 \
			   .cf = LV_COLOR_FORMAT_I1,                                       \
			   .w = ICON_SIZE,                                                 \
			   .h = ICON_SIZE,                                                 \
			   .stride = 1},                                                   \
		.data_size = sizeof(map), .data = (map),                                   \
	}

static const lv_image_dsc_t icon_play = ICON_DSC(icon_play_map);
static const lv_image_dsc_t icon_pause = ICON_DSC(icon_pause_map);
static const lv_image_dsc_t icon_stop = ICON_DSC(icon_stop_map);

/* -------------------------------------------------------------------------- */
/* Estado                                                                     */
/* -------------------------------------------------------------------------- */

static lv_obj_t *mini_bar;
static lv_obj_t *mini_icon;
static lv_obj_t *mini_name;
static lv_obj_t *mini_time;

/* -------------------------------------------------------------------------- */
/* Atualização                                                                */
/* -------------------------------------------------------------------------- */

/** @brief Ícone do estado; a carga recusada aparece como parada. */
static const lv_image_dsc_t *state_icon(const struct player_snapshot *snap)
{
	if (snap->load_error != 0) {
		return &icon_stop;
	}

	switch (snap->state) {
	case PLAYER_PLAYING:
		return &icon_play;
	case PLAYER_PAUSED:
		return &icon_pause;
	default:
		return &icon_stop;
	}
}

/**
 * @brief Copia o instantâneo para o mini-player do rodapé da biblioteca.
 *
 * Ícone, nome e decorrido numa linha só: o total e o motivo de um erro ficam
 * para a tela de reprodução, que tem espaço para eles. Sem faixa, o ícone e
 * o tempo somem, e o nome, que cresce no lugar deles, diz o que fazer.
 */
void ui_mini_update(const struct player_snapshot *snap, uint64_t now_us)
{
	const char *name = ui_track_name(snap->track);
	bool idle = (name == NULL);
	char elapsed[MMSS_SIZE];

	lv_obj_set_flag(mini_icon, LV_OBJ_FLAG_HIDDEN, idle);
	lv_obj_set_flag(mini_time, LV_OBJ_FLAG_HIDDEN, idle);

	if (idle) {
		ui_label_set(mini_name, CHOOSE_TRACK);
		lv_bar_set_value(mini_bar, 0, LV_ANIM_OFF);
		return;
	}

	const lv_image_dsc_t *icon = state_icon(snap);

	if (lv_image_get_src(mini_icon) != icon) {
		lv_image_set_src(mini_icon, icon);
	}

	ui_label_set(mini_name, name);

	if (snap->load_error != 0) {
		ui_label_set(mini_time, "erro");
	} else {
		ui_format_mmss(elapsed, snapshot_elapsed_us(snap, now_us));
		ui_label_set(mini_time, elapsed);
	}

	lv_bar_set_value(mini_bar, snapshot_progress_permille(snap, now_us), LV_ANIM_OFF);
}

/* -------------------------------------------------------------------------- */
/* Construção                                                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Mini-player no rodapé da biblioteca: divisória, barra e uma linha.
 *
 * ```
 *   ESCALA           <- última linha da lista
 *  ________________  <- divisória
 *  (=======-------)  <- progresso
 *  > PAUSAS    0:42  <- ícone, nome, decorrido
 * ```
 *
 * A divisória é criada por último, e fica depois da linha do rodapé entre os
 * filhos da tela.
 */
void ui_mini_build(lv_obj_t *screen)
{
	mini_bar = ui_progress_bar_create(screen, MINI_BAR_HEIGHT);
	lv_obj_align(mini_bar, LV_ALIGN_BOTTOM_MID, 0, -(MINI_ROW_HEIGHT + MINI_GAP));

	lv_obj_t *row = lv_obj_create(screen);

	lv_obj_remove_style_all(row);
	lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(row, LV_HOR_RES, MINI_ROW_HEIGHT);
	lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(row, 3, 0);

	mini_icon = lv_image_create(row);
	lv_image_set_src(mini_icon, &icon_stop);

	/* O nome cresce até o tempo; um nome comprido termina em reticências
	 * e o tempo continua à vista.
	 */
	mini_name = ui_label_create(row);
	lv_obj_set_flex_grow(mini_name, 1);
	lv_obj_set_height(mini_name, MINI_ROW_HEIGHT);
	lv_label_set_long_mode(mini_name, LV_LABEL_LONG_MODE_DOTS);
	ui_label_set(mini_name, CHOOSE_TRACK);

	mini_time = ui_label_create(row);

	lv_obj_add_flag(mini_icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(mini_time, LV_OBJ_FLAG_HIDDEN);

	/* Divisória de 1 px entre a lista e o mini-player, na largura toda. */
	lv_obj_t *divider = lv_obj_create(screen);

	lv_obj_remove_style_all(divider);
	lv_obj_set_style_bg_color(divider, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
	lv_obj_set_size(divider, LV_HOR_RES, DIVIDER_HEIGHT);
	lv_obj_align(divider, LV_ALIGN_BOTTOM_MID, 0,
		     -(MINI_ROW_HEIGHT + MINI_GAP + MINI_BAR_HEIGHT + MINI_GAP));
}

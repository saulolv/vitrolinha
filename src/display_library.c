/**
 * @file display_library.c
 * @brief A lista virtual da tela de biblioteca.
 *
 * O destaque é o foco, que só o encoder move; a faixa do instantâneo leva
 * `>`, e a recusada leva `!`. Foco e marcador são coisas diferentes de
 * propósito: trocar de faixa pelos botões não mexe no foco de quem está
 * navegando.
 *
 * A lista sai de @ref library_copy e é relida quando a
 * @ref library_generation muda: o cartão entra e sai com a tela no ar, e
 * fora de ::LIBRARY_READY a primeira linha diz o estado do cartão.
 */

#include <vitrolinha/cmd.h>
#include <vitrolinha/library.h>
#include <vitrolinha/snapshot.h>
#include <vitrolinha/track.h>

#include <lvgl.h>

#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "display_internal.h"

LOG_MODULE_DECLARE(display, LOG_LEVEL_INF);

/** @brief Texto de uma linha da lista: marcador, espaço, nome e terminador. */
#define ROW_TEXT_SIZE (2U + TRACK_NAME_MAX)

/** @brief Biblioteca, na ordem de @ref library_copy. Tamanho fixo. */
static struct track_meta library[LIBRARY_MAX];

/** @brief Quantas posições de @ref library a lista mostra. Zero sem cartão. */
static size_t library_count;

/**
 * @brief Quantos nomes de @ref library ainda valem para a faixa que toca.
 *
 * Só muda com uma lista nova. Com o cartão fora a lista esvazia, mas a faixa
 * já está em RAM e segue tocando: a reprodução continua com o nome dela.
 */
static size_t named_count;

/** @brief Estado da biblioteca na última cópia. Decide o texto da lista vazia. */
static enum library_state library_status = LIBRARY_NO_CARD;

/** @brief Geração da última cópia; ver @ref ui_library_sync. */
static uint32_t library_seen;

/** @brief Já houve uma cópia? A geração inicial não diz nada sozinha. */
static bool library_synced;

/**
 * @brief Faixas marcadas com `!`: as que a varredura já sabe inválidas e as
 *        que o parser recusou na carga desde a última varredura.
 */
static bool rejected[LIBRARY_MAX];

/** @brief Faixa que leva `>` na lista agora. */
static int16_t marked_track = TRACK_NONE;

/** @brief A lista: alvo do encoder na biblioteca, com as linhas visíveis. */
static lv_obj_t *list;

/** @brief As linhas visíveis; mostram as faixas de @ref top em diante. */
static lv_obj_t *rows[LIBRARY_ROWS];

/** @brief Barra de rolagem, na borda direita da lista. */
static lv_obj_t *thumb;

/** @brief Faixa sob o destaque. */
static size_t cursor;

/** @brief Primeira faixa visível. */
static size_t top;

/* -------------------------------------------------------------------------- */
/* Biblioteca                                                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Copia a biblioteca publicada, se ela mudou desde a última cópia.
 *
 * Não lê o cartão: quem varre é o monitor do cartão (ADR 0010), e a cópia
 * só segura a trava da lista pelo tempo de copiar 32 entradas. Uma
 * publicação entre ler a geração e copiar é vista de novo no quadro
 * seguinte, porque a geração guardada é a de antes.
 *
 * @return true se a lista mudou.
 */
bool ui_library_sync(void)
{
	uint32_t generation = library_generation();

	if (library_synced && (generation == library_seen)) {
		return false;
	}

	library_seen = generation;
	library_synced = true;
	library_status = library_state();

	if (library_status != LIBRARY_READY) {
		/* Fica a lista anterior em @ref library, para o nome da faixa
		 * que ainda toca; só a lista na tela esvazia.
		 */
		LOG_WRN("Biblioteca: %s", library_state_text(library_status));
		library_count = 0U;
		memset(rejected, 0, sizeof(rejected));
		return true;
	}

	/* Fora de argumento inválido, que aqui não há, a cópia não falha. */
	library_count = (size_t)library_copy(library, ARRAY_SIZE(library), NULL);
	named_count = library_count;

	/* O que a varredura já sabe inválido (cabeçalho errado, maior que o
	 * buffer) e o que o parser já recusou chegam marcados; recusas novas
	 * se juntam pelo instantâneo, em @ref ui_library_mark.
	 */
	memset(rejected, 0, sizeof(rejected));
	for (size_t i = 0; i < library_count; i++) {
		rejected[i] = !library[i].valid;
		/* A lista no console, para a bancada conferir índices e nomes. */
		LOG_INF("  [%zu] %s%s", i, library[i].name, library[i].valid ? "" : " (invalida)");
	}

	return true;
}

static bool track_listed(int track)
{
	return (track >= 0) && ((size_t)track < library_count);
}

/** @brief A faixa tem nome? Vale também com o cartão fora; ver @ref named_count. */
static bool track_named(int track)
{
	return (track >= 0) && ((size_t)track < named_count);
}

const char *ui_track_name(int track)
{
	return track_named(track) ? library[track].name : NULL;
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
 * @brief Desenha a janela da biblioteca nas linhas visíveis.
 *
 * Lista virtual: existem só ::LIBRARY_ROWS rótulos, e eles mostram as faixas
 * de @ref top em diante, com o destaque na do @ref cursor. A memória da lista
 * não depende do tamanho da biblioteca, e só os rótulos cujo texto muda são
 * redesenhados.
 */
static void list_render(void)
{
	for (size_t r = 0; r < LIBRARY_ROWS; r++) {
		size_t i = top + r;
		bool shown = (i < library_count);

		lv_obj_set_flag(rows[r], LV_OBJ_FLAG_HIDDEN, !shown);
		lv_obj_set_state(rows[r], LV_STATE_CHECKED, shown && (i == cursor));
		if (shown) {
			char text[ROW_TEXT_SIZE];

			snprintf(text, sizeof(text), "%c %s", track_marker(i, marked_track),
				 library[i].name);
			ui_label_set(rows[r], text);
		}
	}

	/* Biblioteca vazia: a primeira linha diz por quê, sem destaque. */
	if (library_count == 0U) {
		lv_obj_remove_flag(rows[0], LV_OBJ_FLAG_HIDDEN);
		ui_label_set(rows[0], (library_status != LIBRARY_READY)
					      ? library_state_text(library_status)
					      : "Nenhuma faixa");
	}

	/* A barra de rolagem: altura proporcional à janela, posição à dela. Só
	 * aparece quando a biblioteca não cabe na tela.
	 */
	bool scrolls = (library_count > LIBRARY_ROWS);

	lv_obj_set_flag(thumb, LV_OBJ_FLAG_HIDDEN, !scrolls);
	if (scrolls) {
		int32_t track_h = LIBRARY_ROWS * ROW_HEIGHT;
		int32_t thumb_h = track_h * LIBRARY_ROWS / (int32_t)library_count;
		int32_t last_top = (int32_t)(library_count - LIBRARY_ROWS);

		lv_obj_set_height(thumb, thumb_h);
		lv_obj_set_y(thumb, (track_h - thumb_h) * (int32_t)top / last_top);
	}
}

/**
 * @brief Move o cursor @p step faixas, sem dar a volta nas pontas.
 *
 * A janela só anda quando o cursor passa da borda dela: o destaque desce
 * linha por linha, e a lista rola na última.
 */
static void list_move(int step)
{
	if (library_count == 0U) {
		return;
	}

	int next = (int)cursor + step;

	cursor = (size_t)CLAMP(next, 0, (int)library_count - 1);
	if (cursor < top) {
		top = cursor;
	} else if (cursor >= top + LIBRARY_ROWS) {
		top = cursor - LIBRARY_ROWS + 1U;
	}

	list_render();
}

/**
 * @brief Acomoda cursor e janela a uma lista nova e a redesenha.
 *
 * O cursor fica na mesma posição se ela ainda existe, e a janela nunca
 * deixa linhas vazias no fim de uma lista que enche a tela. Sem lista, os
 * dois ficam onde estavam: reinserido o mesmo cartão, o destaque volta à
 * mesma faixa.
 */
static void library_fit(void)
{
	if (library_count > 0U) {
		size_t last_top = (library_count > LIBRARY_ROWS)
					  ? (library_count - LIBRARY_ROWS) : 0U;

		cursor = MIN(cursor, library_count - 1U);
		top = MIN(top, MIN(cursor, last_top));
	}

	list_render();
}

void ui_library_refresh(void)
{
	if (ui_library_sync()) {
		library_fit();
	}
}

void ui_library_cue(int track)
{
	if (track_listed(track)) {
		list_move(track - (int)cursor);
	}
}

/**
 * @brief Reescreve os marcadores da lista se a faixa ou as recusas mudaram.
 *
 * O cursor fica onde o encoder o deixou: trocar de faixa pelos botões não
 * mexe em quem navega.
 */
void ui_library_mark(const struct player_snapshot *snap)
{
	bool dirty = (snap->track != marked_track);

	if (track_listed(snap->track) && ui_is_parser_error(snap->load_error) &&
	    !rejected[snap->track]) {
		rejected[snap->track] = true;
		dirty = true;
	}

	if (!dirty) {
		return;
	}

	marked_track = snap->track;
	list_render();
}

/* -------------------------------------------------------------------------- */
/* Eventos de interação                                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Clique na lista: toca a faixa sob o destaque e abre a reprodução.
 *
 * Publica ::CMD_SELECT; o que a reprodução mostra é o que o player publicar
 * no instantâneo em resposta — a tela mostra o que está tocando, não o que
 * foi pedido.
 */
static void list_clicked(void)
{
	if (library_count == 0U) {
		return;
	}

	int err = cmd_post(CMD_SELECT, (int16_t)library[cursor].index);

	if (err != 0) {
		LOG_WRN("Selecao da faixa %u descartada: %d", library[cursor].index, err);
		return;
	}

	ui_show_playback();
}

/**
 * @brief Eventos do encoder na lista.
 *
 * A lista é o único objeto do grupo, em edição: o giro chega como seta, na
 * placa e no keypad do simulador, e o clique, como clique. É o mesmo arranjo
 * da tela de reprodução.
 */
static void list_event_cb(lv_event_t *e)
{
	if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
		list_clicked();
		return;
	}

	uint32_t key = lv_event_get_key(e);

	if (key == LV_KEY_LEFT) {
		list_move(-1);
	} else if (key == LV_KEY_RIGHT) {
		list_move(1);
	}
}

/* -------------------------------------------------------------------------- */
/* Construção                                                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Lista da biblioteca: o destaque anda, e a lista rola nas bordas.
 *
 * É o padrão dos menus navegados por roda ou botão: a tela fica sempre cheia
 * de faixas, e o destaque mostra onde se está. Um roller, que prende o
 * destaque no meio, é para escolher um valor, e deixava linhas vazias nas
 * pontas da lista.
 *
 * Tudo sem os estilos do tema: o destaque é a linha invertida, e nada mais.
 * O contorno de foco do tema, desenhado 2 px por fora do objeto focado,
 * sobrava como uma faixa colada na barra do mini-player.
 *
 * @return O grupo da tela, com a lista em edição.
 */
static lv_group_t *list_build(lv_obj_t *screen)
{
	static lv_style_t style_row;
	static lv_style_t style_row_cursor;
	static bool styled;

	if (!styled) {
		lv_style_init(&style_row);
		lv_style_set_text_color(&style_row, lv_color_black());
		lv_style_set_pad_hor(&style_row, 1);

		lv_style_init(&style_row_cursor);
		lv_style_set_bg_color(&style_row_cursor, lv_color_black());
		lv_style_set_bg_opa(&style_row_cursor, LV_OPA_COVER);
		lv_style_set_text_color(&style_row_cursor, lv_color_white());

		styled = true;
	}

	list = lv_obj_create(screen);
	lv_obj_remove_style_all(list);
	lv_obj_set_size(list, LV_HOR_RES, LIBRARY_ROWS * ROW_HEIGHT);
	lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 0);
	lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
	/* Sem rolagem própria — quem rola é a janela —, o que também faz o
	 * encoder entregar o clique como clique, e não como entrada em edição.
	 */
	lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);
	/* Espaço para a barra de rolagem: o destaque para antes dela. */
	lv_obj_set_style_pad_right(list, 3, 0);
	lv_obj_add_event_cb(list, list_event_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_add_event_cb(list, list_event_cb, LV_EVENT_KEY, NULL);

	for (size_t r = 0; r < LIBRARY_ROWS; r++) {
		rows[r] = ui_label_create(list);
		lv_obj_remove_style_all(rows[r]);
		lv_obj_add_style(rows[r], &style_row, 0);
		lv_obj_add_style(rows[r], &style_row_cursor, LV_STATE_CHECKED);
		/* Altura fixa: as reticências só valem com ela. Com altura
		 * automática o LVGL quebra o nome comprido em duas linhas, e a
		 * linha empurra o resto da lista.
		 */
		lv_obj_set_size(rows[r], lv_pct(100), ROW_HEIGHT);
		lv_label_set_long_mode(rows[r], LV_LABEL_LONG_MODE_DOTS);
	}

	/* Barra de rolagem de 2 px na borda direita, desenhada à mão: com a
	 * lista virtual, não há conteúdo rolando para o LVGL medir.
	 */
	thumb = lv_obj_create(screen);
	lv_obj_remove_style_all(thumb);
	lv_obj_set_style_bg_color(thumb, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, 0);
	lv_obj_set_width(thumb, 2);
	lv_obj_set_x(thumb, LV_HOR_RES - 2);

	lv_group_t *group = lv_group_create();

	lv_group_add_obj(group, list);
	lv_group_set_editing(group, true);

	cursor = 0;
	top = 0;
	list_render();

	return group;
}

lv_group_t *ui_library_build(lv_obj_t *screen)
{
	marked_track = TRACK_NONE;

	/* A lista no alto e o mini-player embaixo; ver a geometria em
	 * display_internal.h.
	 */
	lv_group_t *group = list_build(screen);

	ui_mini_build(screen);

	return group;
}

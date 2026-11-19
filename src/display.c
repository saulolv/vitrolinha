/**
 * @file display.c
 * @brief Interface no OLED: tela de biblioteca e tela de reprodução.
 *
 * Duas telas, como a especificação pede:
 *
 * - **Biblioteca**: a lista de faixas e, no rodapé, um mini-player. O
 *   destaque é o foco, que só o encoder move; a faixa do instantâneo leva
 *   `>`, e a recusada leva `!`. Foco e marcador são coisas diferentes de
 *   propósito: trocar de faixa pelos botões não mexe no foco de quem está
 *   navegando. O mini-player mostra o que toca sem sair da lista.
 * - **Reprodução**: a visão detalhada — nome, estado, tempo decorrido e
 *   total, a barra e o motivo de uma carga recusada.
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

/** @brief Linhas visíveis da biblioteca. */
#define LIBRARY_ROWS 5

/** @brief Altura de uma linha da lista: a da UNSCII-8, 8 px de glifo e 1 de respiro. */
#define ROW_HEIGHT 9

/** @brief Texto de uma linha da lista: marcador, espaço, nome e terminador. */
#define ROW_TEXT_SIZE (2U + TRACK_NAME_MAX)

/** @brief Texto do rótulo de nome quando o instantâneo não tem faixa. */
#define NOTHING_PLAYING "Nada tocando"

/** @brief O mini-player sem faixa: cabe nas 16 colunas, e diz o que fazer. */
#define CHOOSE_TRACK "Escolha a faixa"

/*
 * Geometria da biblioteca, de cima para baixo, nos 64 px da tela: 5 linhas x
 * 9 px = 45 px de lista, 2 px de folga, a barra de 6 px, mais 2 de folga e o
 * rodapé de 9 px. O teste de geometria do display confere a soma, e o
 * desenho, que as folgas ficam apagadas.
 */

/** @brief Altura da barra do mini-player, que também separa lista e rodapé. */
#define MINI_BAR_HEIGHT 6

/** @brief Folga entre a barra e o que está acima e abaixo dela. */
#define MINI_BAR_GAP 2

/** @brief Altura da barra da tela de reprodução, que tem espaço de sobra. */
#define PLAYBACK_BAR_HEIGHT 8

/** @brief Altura do rodapé do mini-player: uma linha da fonte. */
#define MINI_ROW_HEIGHT 9

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

/** @brief Biblioteca, na ordem de @ref storage_scan. Tamanho fixo. */
static struct track_meta library[LIBRARY_MAX];

/** @brief Quantas posições de @ref library valem. Zero se a varredura falhou. */
static size_t library_count;

/** @brief Erro da varredura, ou 0. Decide o texto da lista vazia. */
static int library_error;

/**
 * @brief Faixas marcadas com `!`: as que a varredura já sabe inválidas e as
 *        que o parser recusou na carga desde a última varredura.
 */
static bool rejected[LIBRARY_MAX];

/** @brief Faixa que leva `>` na lista agora. */
static int16_t marked_track = TRACK_NONE;

/** @brief Pedido de alternar a tela, vindo do botão; atendido no timer do LVGL. */
static atomic_t toggle_requested;

static lv_indev_t *indev;

static lv_obj_t *library_screen;
static lv_group_t *library_group;

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

/* Mini-player no rodapé da biblioteca. */
static lv_obj_t *mini_bar;
static lv_obj_t *mini_icon;
static lv_obj_t *mini_name;
static lv_obj_t *mini_time;

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

static void label_set(lv_obj_t *label, const char *text);

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
			label_set(rows[r], text);
		}
	}

	/* Biblioteca vazia: a primeira linha diz por quê, sem destaque. */
	if (library_count == 0U) {
		lv_obj_remove_flag(rows[0], LV_OBJ_FLAG_HIDDEN);
		label_set(rows[0], (library_error != 0) ? storage_error_text(library_error)
							: "Biblioteca vazia");
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
 * @brief Reescreve os marcadores da lista se a faixa ou as recusas mudaram.
 *
 * O cursor fica onde o encoder o deixou: trocar de faixa pelos botões não
 * mexe em quem navega.
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
	list_render();
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

/** @brief Tamanho de um tempo `m:ss`; o maior, 71582:47, vem de 32 bits de µs. */
#define MMSS_SIZE 12

/** @brief Escreve @p us como `m:ss`, em minutos e segundos inteiros. */
static void format_mmss(char out[MMSS_SIZE], uint32_t us)
{
	uint32_t s = us / USEC_PER_SEC;

	snprintf(out, MMSS_SIZE, "%u:%02u", s / 60U, s % 60U);
}

/** @brief Copia o instantâneo para os rótulos e a barra da reprodução. */
static void playback_update(const struct player_snapshot *snap, uint64_t now_us)
{
	char text[2 * MMSS_SIZE + 3];

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
		char elapsed[MMSS_SIZE];
		char total[MMSS_SIZE];

		format_mmss(elapsed, snapshot_elapsed_us(snap, now_us));
		format_mmss(total, snap->total_us);
		snprintf(text, sizeof(text), "%s / %s", elapsed, total);
	}
	label_set(time_label, text);

	lv_bar_set_value(bar, snapshot_progress_permille(snap, now_us), LV_ANIM_OFF);
}

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
static void mini_update(const struct player_snapshot *snap, uint64_t now_us)
{
	bool idle = !track_listed(snap->track);
	char elapsed[MMSS_SIZE];

	lv_obj_set_flag(mini_icon, LV_OBJ_FLAG_HIDDEN, idle);
	lv_obj_set_flag(mini_time, LV_OBJ_FLAG_HIDDEN, idle);

	if (idle) {
		label_set(mini_name, CHOOSE_TRACK);
		lv_bar_set_value(mini_bar, 0, LV_ANIM_OFF);
		return;
	}

	const lv_image_dsc_t *icon = state_icon(snap);

	if (lv_image_get_src(mini_icon) != icon) {
		lv_image_set_src(mini_icon, icon);
	}

	label_set(mini_name, library[snap->track].name);

	if (snap->load_error != 0) {
		label_set(mini_time, "erro");
	} else {
		format_mmss(elapsed, snapshot_elapsed_us(snap, now_us));
		label_set(mini_time, elapsed);
	}

	lv_bar_set_value(mini_bar, snapshot_progress_permille(snap, now_us), LV_ANIM_OFF);
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
	mini_update(&snap, now_us);
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

	show_playback();
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

/**
 * @brief Barra de progresso no visual das duas telas.
 *
 * O do tema mono, com as pontas arredondadas e 90% da largura. Um contorno
 * quadrado na largura toda foi tentado no mini-player e ficou pesado no
 * OLED; esta é a barra que já estava em uso, agora a mesma nos dois lugares.
 */
static lv_obj_t *progress_bar_create(lv_obj_t *parent, int32_t height)
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

/**
 * @brief Mini-player no rodapé da biblioteca: barra e, abaixo, uma linha.
 *
 * ```
 *   ESCALA          <- última linha da lista
 *  (=======-------)  <- progresso, que também separa lista e rodapé
 *  > PAUSAS    0:42  <- ícone, nome, decorrido
 * ```
 */
static void mini_build(void)
{
	mini_bar = progress_bar_create(library_screen, MINI_BAR_HEIGHT);
	lv_obj_align(mini_bar, LV_ALIGN_BOTTOM_MID, 0, -(MINI_ROW_HEIGHT + MINI_BAR_GAP));

	lv_obj_t *row = lv_obj_create(library_screen);

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
	mini_name = lv_label_create(row);
	lv_obj_set_flex_grow(mini_name, 1);
	lv_label_set_long_mode(mini_name, LV_LABEL_LONG_MODE_DOTS);
	lv_label_set_text(mini_name, CHOOSE_TRACK);

	mini_time = lv_label_create(row);
	lv_label_set_text(mini_time, "");

	lv_obj_add_flag(mini_icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(mini_time, LV_OBJ_FLAG_HIDDEN);
}

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
 */
static void list_build(void)
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

	list = lv_obj_create(library_screen);
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
		rows[r] = lv_label_create(list);
		lv_obj_remove_style_all(rows[r]);
		lv_obj_add_style(rows[r], &style_row, 0);
		lv_obj_add_style(rows[r], &style_row_cursor, LV_STATE_CHECKED);
		lv_obj_set_width(rows[r], lv_pct(100));
		lv_label_set_long_mode(rows[r], LV_LABEL_LONG_MODE_DOTS);
	}

	/* Barra de rolagem de 2 px na borda direita, desenhada à mão: com a
	 * lista virtual, não há conteúdo rolando para o LVGL medir.
	 */
	thumb = lv_obj_create(library_screen);
	lv_obj_remove_style_all(thumb);
	lv_obj_set_style_bg_color(thumb, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, 0);
	lv_obj_set_width(thumb, 2);
	lv_obj_set_x(thumb, LV_HOR_RES - 2);

	library_group = lv_group_create();
	lv_group_add_obj(library_group, list);
	lv_group_set_editing(library_group, true);

	cursor = 0;
	top = 0;
	list_render();
}

static void library_build(void)
{
	library_screen = screen_create();
	marked_track = TRACK_NONE;

	/* A lista no alto e o mini-player embaixo; ver a geometria no topo. */
	list_build();
	mini_build();
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

	bar = progress_bar_create(playback_screen, PLAYBACK_BAR_HEIGHT);

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

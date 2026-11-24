/**
 * @file display_internal.h
 * @brief O que os arquivos da interface dividem entre si.
 *
 * Não é contrato com o resto do firmware — esse é o
 * include/vitrolinha/display.h. Só os arquivos display*.c de src/ incluem
 * este cabeçalho:
 *
 * | Arquivo              | O que faz                                                    |
 * |----------------------|--------------------------------------------------------------|
 * | display.c            | `display_init`: confere o display, monta a interface, liga   |
 * | display_ui.c         | Núcleo: telas, navegação, timer de atualização, rótulos      |
 * | display_library.c    | A lista virtual da biblioteca                                |
 * | display_mini.c       | O mini-player no rodapé da biblioteca                        |
 * | display_playback.c   | A tela de reprodução                                         |
 * | display_fps_test.c   | No lugar dos quatro de cima, com `CONFIG_VITROLINHA_DISPLAY_FPS_TEST` |
 */

#ifndef VITROLINHA_DISPLAY_INTERNAL_H_
#define VITROLINHA_DISPLAY_INTERNAL_H_

#include <vitrolinha/snapshot.h>

#include <lvgl.h>

#include <stdbool.h>
#include <stdint.h>

/*
 * Geometria da biblioteca, de cima para baixo, nos 64 px da tela:
 *
 *    0-35  lista, 4 linhas x 9 px
 *   36-39  folga
 *      40  linha divisória, na largura toda
 *   41-44  folga
 *   45-50  barra de progresso
 *   51-54  folga
 *   55-63  rodapé: ícone, nome, decorrido
 *
 * O teste de geometria do display confere a soma, e o desenho, que as
 * folgas ficam apagadas e a divisória, acesa.
 */

/**
 * @brief Linhas visíveis da biblioteca.
 *
 * Quatro, e não cinco: a quinta linha custava o respiro entre a lista e o
 * mini-player, que ficavam colados.
 */
#define LIBRARY_ROWS 4

/** @brief Altura de uma linha da lista: a da UNSCII-8, 8 px de glifo e 1 de respiro. */
#define ROW_HEIGHT 9

/** @brief Altura da barra do mini-player. */
#define MINI_BAR_HEIGHT 6

/** @brief Folga entre os blocos do pé da biblioteca: lista, divisória, barra, rodapé. */
#define MINI_GAP 4

/** @brief Espessura da divisória entre a lista e o mini-player. */
#define DIVIDER_HEIGHT 1

/** @brief Altura da barra da tela de reprodução, que tem espaço de sobra. */
#define PLAYBACK_BAR_HEIGHT 8

/** @brief Altura do rodapé do mini-player: uma linha da fonte. */
#define MINI_ROW_HEIGHT 9

/** @brief Tamanho de um tempo `m:ss`; o maior, 71582:47, vem de 32 bits de µs. */
#define MMSS_SIZE 12

/* -------------------------------------------------------------------------- */
/* Chamadas por display.c                                                     */
/* -------------------------------------------------------------------------- */

/** @brief O que precisa estar pronto antes de montar as telas, fora do lock do LVGL. */
void ui_prepare(void);

/** @brief Monta a interface. Com o lock do LVGL adquirido. */
void ui_build(void);

/* -------------------------------------------------------------------------- */
/* display_ui.c                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Cria um rótulo cujo texto @ref ui_label_set compara antes de
 *        reescrever.
 */
lv_obj_t *ui_label_create(lv_obj_t *parent);

/** @brief Só reescreve o rótulo quando o texto muda, para não redesenhar à toa. */
void ui_label_set(lv_obj_t *label, const char *text);

/** @brief Barra de progresso no visual das duas telas. */
lv_obj_t *ui_progress_bar_create(lv_obj_t *parent, int32_t height);

/** @brief Escreve @p us como `m:ss`, em minutos e segundos inteiros. */
void ui_format_mmss(char out[MMSS_SIZE], uint32_t us);

/** @brief Erro de carga que é do conteúdo do arquivo, e não do cartão. */
bool ui_is_parser_error(int err);

/** @brief Mostra a biblioteca e passa o encoder ao grupo dela. */
void ui_show_library(void);

/** @brief Mostra a reprodução e passa o encoder ao grupo dela. */
void ui_show_playback(void);

/* -------------------------------------------------------------------------- */
/* display_library.c                                                          */
/* -------------------------------------------------------------------------- */

/** @brief Copia a biblioteca publicada, se mudou. @return true se a lista mudou. */
bool ui_library_sync(void);

/** @brief Copia a biblioteca, se mudou, e acomoda a lista a ela. */
void ui_library_refresh(void);

/** @brief Leva o destaque à faixa @p track, se ela está na lista. */
void ui_library_cue(int track);

/** @brief Reescreve os marcadores da lista se a faixa ou as recusas mudaram. */
void ui_library_mark(const struct player_snapshot *snap);

/**
 * @brief Nome da faixa, que vale também com o cartão fora.
 *
 * @return O nome, ou NULL se a faixa não tem nome na última lista.
 */
const char *ui_track_name(int track);

/** @brief Cria a lista e o mini-player em @p screen. @return O grupo da tela. */
lv_group_t *ui_library_build(lv_obj_t *screen);

/* -------------------------------------------------------------------------- */
/* display_mini.c                                                             */
/* -------------------------------------------------------------------------- */

/** @brief Cria o mini-player no rodapé de @p screen. */
void ui_mini_build(lv_obj_t *screen);

/** @brief Copia o instantâneo para o mini-player. */
void ui_mini_update(const struct player_snapshot *snap, uint64_t now_us);

/* -------------------------------------------------------------------------- */
/* display_playback.c                                                         */
/* -------------------------------------------------------------------------- */

/** @brief Monta a tela de reprodução em @p screen. @return O grupo da tela. */
lv_group_t *ui_playback_build(lv_obj_t *screen);

/** @brief Copia o instantâneo para os rótulos e a barra da reprodução. */
void ui_playback_update(const struct player_snapshot *snap, uint64_t now_us);

#endif /* VITROLINHA_DISPLAY_INTERNAL_H_ */

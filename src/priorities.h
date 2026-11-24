/**
 * @file priorities.h
 * @brief Prioridades das threads do Vitrolinha, num lugar só.
 *
 * Número menor é mais prioritário, como no Zephyr. A ordem é a tese do
 * trabalho: nada de cartão, de tela ou de flash pode passar na frente do
 * `player`. O callback do temporizador, que dispara cada nota, roda em
 * interrupção e não aparece aqui — está acima de todas.
 *
 * | Thread              | Prioridade | Por que nesse lugar                         |
 * |---------------------|------------|---------------------------------------------|
 * | `player`            | 4          | Comando de transporte não espera ninguém    |
 * | `loader`            | 8          | Repõe a fila do player; espera só por ele   |
 * | fila do LVGL        | 10         | Redesenho atrasado é tolerável; nota, não   |
 * | `status_led`        | 11         | Cor atrasada de um quadro não é defeito     |
 * | monitor do `card`   | 12         | Montar bloqueia até 1,5 s                   |
 * | `last_track`        | 13         | Gravar na flash nunca é urgente             |
 *
 * Cada módulo usa a sua constante daqui, e os comentários deles apontam para
 * este arquivo em vez de citar o número dos outros de cabeça. Os
 * `BUILD_ASSERT` abaixo travam a ordem: trocar um número que a inverta
 * reprova o build.
 *
 * A do LVGL não é definida por nenhum fonte do projeto: quem a fixa é
 * `CONFIG_LV_Z_LVGL_WORKQUEUE_PRIORITY`, no prj.conf, cujo padrão é 0 e
 * sobrepujaria o player. A constante daqui é o espelho dela, e onde o LVGL
 * está ligado um `BUILD_ASSERT` confere que os dois concordam.
 *
 * Interno a src/, e não em include/vitrolinha/: nenhuma prioridade faz parte
 * do contrato de um módulo com os outros, e nenhum teste precisa delas.
 */

#ifndef VITROLINHA_PRIORITIES_H_
#define VITROLINHA_PRIORITIES_H_

#include <zephyr/toolchain.h>

/** @brief A thread `player` (src/player.c). */
#define PRIORITY_PLAYER 4

/** @brief O worker do `loader` (src/loader.c). */
#define PRIORITY_LOADER 8

/** @brief A fila de trabalho do LVGL; espelho de `CONFIG_LV_Z_LVGL_WORKQUEUE_PRIORITY`. */
#define PRIORITY_LVGL 10

/** @brief A thread dos LEDs de estado (src/status_led.c). */
#define PRIORITY_STATUS_LED 11

/** @brief O monitor do cartão (src/card.c). */
#define PRIORITY_CARD_MONITOR 12

/** @brief A gravação da última faixa (src/last_track.c). */
#define PRIORITY_LAST_TRACK 13

BUILD_ASSERT(PRIORITY_PLAYER < PRIORITY_LOADER, "o player tem de ficar acima do loader");
BUILD_ASSERT(PRIORITY_LOADER < PRIORITY_LVGL, "o loader tem de ficar acima do LVGL");
BUILD_ASSERT(PRIORITY_LVGL < PRIORITY_STATUS_LED, "o LVGL tem de ficar acima dos LEDs");
BUILD_ASSERT(PRIORITY_STATUS_LED < PRIORITY_CARD_MONITOR,
	     "os LEDs têm de ficar acima do monitor do cartão");
BUILD_ASSERT(PRIORITY_CARD_MONITOR < PRIORITY_LAST_TRACK,
	     "o monitor do cartão tem de ficar acima da última faixa");

#if defined(CONFIG_LV_Z_LVGL_WORKQUEUE_PRIORITY)
BUILD_ASSERT(CONFIG_LV_Z_LVGL_WORKQUEUE_PRIORITY == PRIORITY_LVGL,
	     "CONFIG_LV_Z_LVGL_WORKQUEUE_PRIORITY saiu de PRIORITY_LVGL: ver src/priorities.h");
#endif

#endif /* VITROLINHA_PRIORITIES_H_ */

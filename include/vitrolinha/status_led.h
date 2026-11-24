/**
 * @file status_led.h
 * @brief O estado da reprodução nos LEDs RGB (RF11).
 *
 * Na ZBook P2 são os quatro WS2812 do nó `ws2812`, no GPIO 31, acionados
 * pelo PIO1 — não pelo PWM, e por isso sem disputa com o buzzer. O módulo
 * fala com qualquer fita do alias `led-strip` pela API `led_strip`; nos
 * testes, com uma fita de mentira que registra as cores.
 *
 * Quem acende os LEDs é uma thread de baixa prioridade que lê o instantâneo
 * (@ref snapshot_read) a cada ::STATUS_LED_PERIOD_MS e só reescreve a fita
 * quando a cor muda. O player não sabe que ela existe, e nada disto roda no
 * callback do temporizador.
 *
 * | Estado | Cor |
 * |---|---|
 * | tocando | verde |
 * | pausado | âmbar |
 * | parado com erro de carga, ou biblioteca fora de ::LIBRARY_READY | vermelho |
 * | parado | apagado |
 *
 * O brilho é baixo, ::STATUS_LED_LEVEL de 255: os WS2812 a pleno ofuscam a
 * centímetros do OLED.
 */

#ifndef VITROLINHA_STATUS_LED_H_
#define VITROLINHA_STATUS_LED_H_

#include <zephyr/drivers/led_strip.h>

#include <vitrolinha/library.h>
#include <vitrolinha/snapshot.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Período da leitura do instantâneo: o mesmo da tela. */
#define STATUS_LED_PERIOD_MS 100

/** @brief Intensidade do canal mais aceso, de 0 a 255. */
#define STATUS_LED_LEVEL 16U

/**
 * @brief A cor de um estado. Função pura: é a tabela do cabeçalho.
 *
 * @param snap    Instantâneo lido.
 * @param library Estado da biblioteca.
 * @return A cor dos quatro LEDs.
 */
struct led_rgb status_led_color(const struct player_snapshot *snap, enum library_state library);

/**
 * @brief Confere a fita e põe a thread de pé.
 *
 * @retval 0       Thread no ar.
 * @retval -ENODEV A fita não está pronta; a thread não parte.
 */
int status_led_init(void);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_STATUS_LED_H_ */

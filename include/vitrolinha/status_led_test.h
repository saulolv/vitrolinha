/**
 * @file status_led_test.h
 * @brief API de teste de @ref status_led.h.
 *
 * Este cabeçalho **não** faz parte do contrato: nenhum módulo de produção o
 * inclui, só o próprio status_led.c, cuja thread usa a função a cada
 * leitura, e os testes, que conferem a tabela de cores sem passar pela
 * thread.
 */

#ifndef VITROLINHA_STATUS_LED_TEST_H_
#define VITROLINHA_STATUS_LED_TEST_H_

#include <vitrolinha/status_led.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A cor de um estado. Função pura: é a tabela de @ref status_led.h.
 *
 * @param snap    Instantâneo lido.
 * @param library Estado da biblioteca.
 * @return A cor dos quatro LEDs.
 */
struct led_rgb status_led_color(const struct player_snapshot *snap, enum library_state library);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_STATUS_LED_TEST_H_ */

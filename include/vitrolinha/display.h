/**
 * @file display.h
 * @brief Interface no OLED: tela de biblioteca e tela de reprodução.
 *
 * A lista vem do contrato do @ref storage.h e a tela de reprodução, do
 * instantâneo do player (@ref snapshot.h). A interface não sabe quem responde
 * a nenhum dos dois — cartão, faixas embutidas ou pasta do computador —, e
 * por isso o mesmo código serve à placa e ao simulador.
 *
 * O cabeçalho não arrasta o LVGL: quem só chama @ref display_init não
 * precisa dele.
 */

#ifndef VITROLINHA_DISPLAY_H_
#define VITROLINHA_DISPLAY_H_

#include <zephyr/dt-bindings/input/input-event-codes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Botão que alterna entre biblioteca e reprodução.
 *
 * É da interface, não do transporte: não vira comando do player, e o
 * `buttons.c` o deixa passar sem publicar nada. É o botão 4 da placa, o
 * mais à direita, como a especificação pede (issue #27).
 */
#define DISPLAY_TOGGLE_BUTTON INPUT_BTN_3

/**
 * @brief Quanto tempo a tela de reprodução mostra o erro de carga antes de
 *        voltar à biblioteca, onde a faixa aparece marcada com `!`.
 */
#define DISPLAY_ERROR_MS 2000

/** @brief Período do quadro de demonstração: os 10 fps do plano. */
#define DISPLAY_DEMO_PERIOD_MS 100

/**
 * @brief Período de atualização da faixa e da barra de progresso.
 *
 * Taxa fixa, nunca por nota: a interface lê o instantâneo neste ritmo, e o
 * player não sabe que ela existe.
 */
#define DISPLAY_REFRESH_PERIOD_MS 100

/**
 * @brief Varre a biblioteca, monta as telas e desliga o blanking do display.
 *
 * Chamar depois de `card_init()`: na placa, a lista sai do cartão montado, e
 * antes disso a varredura só encontraria o soquete vazio.
 *
 * Chamada de novo, refaz as telas do zero a partir de uma varredura nova e
 * descarta as anteriores; a interface volta à biblioteca.
 *
 * @retval 0        Tela no ar.
 * @retval -ENODEV  Display não está pronto.
 * @retval outro    Erro devolvido por `display_blanking_off()`.
 */
int display_init(void);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_DISPLAY_H_ */

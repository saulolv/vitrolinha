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
 * `buttons.c` o deixa passar sem publicar nada. Por código, não por posição
 * física — a ordem física dos botões é tarefa de bancada (issue #27).
 */
#define DISPLAY_TOGGLE_BUTTON INPUT_BTN_1

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

/**
 * @brief Põe o destaque da biblioteca numa faixa, sem tocá-la.
 *
 * É a metade visível da faixa lembrada no boot (RF10); a outra é o
 * ::CMD_CUE do player. Só levanta o pedido: quem move o destaque é o timer
 * de atualização, sob o lock do LVGL, como no botão de alternar. Faixa que
 * não está na lista copiada é ignorada.
 *
 * Chamável de qualquer thread, sem bloquear.
 *
 * @param track Índice na biblioteca.
 */
void display_cue(int track);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_DISPLAY_H_ */

/**
 * @file display.h
 * @brief Interface no OLED: biblioteca no roller, faixa e progresso abaixo.
 *
 * A lista vem do contrato do @ref storage.h e a barra, do instantâneo do
 * player (@ref snapshot.h). A interface não sabe quem responde a nenhum dos
 * dois — cartão, faixas embutidas ou pasta do computador —, e por isso o
 * mesmo código serve à placa e ao simulador.
 *
 * O cabeçalho não arrasta o LVGL: quem só chama @ref display_init não
 * precisa dele.
 */

#ifndef VITROLINHA_DISPLAY_H_
#define VITROLINHA_DISPLAY_H_

#ifdef __cplusplus
extern "C" {
#endif

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
 * @brief Varre a biblioteca, monta a tela e desliga o blanking do display.
 *
 * Chamar depois de `card_init()`: na placa, a lista sai do cartão montado, e
 * antes disso a varredura só encontraria o soquete vazio.
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

/**
 * @file board_zbook.h
 * @brief Verificações de compilação específicas da ZBook P2.
 *
 * Incluído apenas quando o alvo é a placa física. Existe como cabeçalho
 * separado para que o alvo de simulação, que não tem nenhum destes nós, não
 * precise de `#if` espalhado pelo código da aplicação.
 *
 * As duas famílias de verificação aqui cobrem as duas maneiras de o build
 * sair errado sem emitir erro.
 */

#ifndef VITROLINHA_BOARD_ZBOOK_H_
#define VITROLINHA_BOARD_ZBOOK_H_

#include <zephyr/devicetree.h>
#include <zephyr/toolchain.h>

/*
 * 1. Revisão da placa.
 *
 * O board.yml da ZBook declara `default: p2`, mas o campo é ignorado em
 * placas de formato `custom`, e o revision.cmake recai em `p1`. Compilar sem
 * o sufixo @p2 produz um binário para uma placa sem encoder e sem tela.
 *
 * O CMakeLists rejeita o alvo errado antes de compilar; estes BUILD_ASSERT
 * pegam o caso de alguém chegar ao build por outro caminho, como um IDE que
 * monte a linha de comando sozinho. Os três nós só existem na revisão P2.
 */
BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(encoder_qdec)),
	     "Sem encoder no devicetree: compile com -b zbook@p2/rp2350b/m33 "
	     "(o sufixo @p2 e obrigatorio).");
BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(enc_button)),
	     "Sem clique de encoder no devicetree: falta o sufixo @p2.");
BUILD_ASSERT(DT_HAS_CHOSEN(zephyr_display),
	     "Sem chosen zephyr,display: falta o sufixo @p2.");

/*
 * 2. Sobreposição da aplicação.
 *
 * Um arquivo em boards/ só é aplicado se o nome casar exatamente com a
 * string de build montada a partir de placa, qualificadores e revisão, e
 * errar o nome não gera aviso nenhum. Sem estas duas verificações, o
 * firmware compilaria com o I²C a 100 kHz e com o cartão sempre "presente".
 */
BUILD_ASSERT(DT_PROP(DT_NODELABEL(i2c0), clock_frequency) == 400000,
	     "boards/zbook_rp2350b_m33_p2.overlay nao foi aplicado: "
	     "i2c0 ainda esta em 100 kHz.");
BUILD_ASSERT(DT_NODE_HAS_PROP(DT_NODELABEL(sdhc0), cd_gpios),
	     "boards/zbook_rp2350b_m33_p2.overlay nao foi aplicado: "
	     "sdhc0 esta sem cd-gpios.");

#endif /* VITROLINHA_BOARD_ZBOOK_H_ */

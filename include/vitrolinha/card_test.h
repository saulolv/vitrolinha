/**
 * @file card_test.h
 * @brief API de teste de @ref card.h.
 *
 * Este cabeçalho **não** faz parte do contrato: nenhum módulo de produção o
 * inclui, só o próprio card.c, que usa a função no próprio log, e os testes.
 */

#ifndef VITROLINHA_CARD_TEST_H_
#define VITROLINHA_CARD_TEST_H_

#include <vitrolinha/card.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Nome do estado, para o log do módulo.
 *
 * @param state Estado a nomear.
 * @return Texto constante, sem acento. Valor fora da enumeração devolve
 *         `"desconhecido"` em vez de indexar fora da tabela.
 */
const char *card_state_name(enum card_state state);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_CARD_TEST_H_ */

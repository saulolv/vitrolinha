/**
 * @file parser_rtttl_test.h
 * @brief API de teste de @ref parser_rtttl.h.
 *
 * Este cabeçalho **não** faz parte do contrato: nenhum módulo de produção o
 * inclui, só o próprio parser_rtttl.c, que usa a função na interpretação e
 * na transposição, e os testes, que conferem a afinação por ela.
 */

#ifndef VITROLINHA_PARSER_RTTTL_TEST_H_
#define VITROLINHA_PARSER_RTTTL_TEST_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Frequência de uma nota, em Hz arredondados.
 *
 * Temperamento igual com Lá 4 em 440 Hz.
 *
 * @param pitch Semitons a partir do Dó 0: `oitava * 12 + nota`, com Dó = 0.
 * @return A frequência, ou 0 para @p pitch acima da oitava 11.
 */
uint32_t parser_rtttl_frequency(unsigned int pitch);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_PARSER_RTTTL_TEST_H_ */

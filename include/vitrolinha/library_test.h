/**
 * @file library_test.h
 * @brief API de teste de @ref library.h.
 *
 * Este cabeçalho **não** faz parte do contrato: nenhum módulo de produção o
 * inclui, só o próprio library.c, para conferir a assinatura, e os testes.
 */

#ifndef VITROLINHA_LIBRARY_TEST_H_
#define VITROLINHA_LIBRARY_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Devolve o módulo ao estado de quem acabou de ligar a placa.
 *
 * Esquece inclusive que algum cartão já foi lido — que é o que separa
 * ::LIBRARY_REMOVED de ::LIBRARY_NO_CARD e ::LIBRARY_REBOOT de
 * ::LIBRARY_UNREADABLE. O firmware não tem motivo para chamar isto; existe
 * para que cada caso de teste comece do boot.
 */
void library_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_LIBRARY_TEST_H_ */

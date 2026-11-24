/**
 * @file cmd_test.h
 * @brief API de teste de @ref cmd.h.
 *
 * Este cabeçalho **não** faz parte do contrato: nenhum módulo de produção o
 * inclui, só o próprio cmd.c, para conferir a assinatura, e os testes. O
 * firmware não tem motivo para chamar o que está aqui.
 */

#ifndef VITROLINHA_CMD_TEST_H_
#define VITROLINHA_CMD_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Descarta todos os comandos pendentes.
 *
 * O firmware não a chama: o player consome cada comando na ordem em que
 * chega. Existe para que cada caso de teste comece com a fila vazia.
 */
void cmd_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_CMD_TEST_H_ */

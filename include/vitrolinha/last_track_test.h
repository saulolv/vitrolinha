/**
 * @file last_track_test.h
 * @brief API de teste de @ref last_track.h.
 *
 * Este cabeçalho **não** faz parte do contrato: nenhum módulo de produção o
 * inclui, só o próprio last_track.c — cuja thread chama @ref last_track_poll
 * a cada ::LAST_TRACK_POLL_MS — e os testes, que a chamam com o relógio na
 * mão em vez de esperar pela thread.
 */

#ifndef VITROLINHA_LAST_TRACK_TEST_H_
#define VITROLINHA_LAST_TRACK_TEST_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Uma passada da thread de gravação.
 *
 * @param now_ms Instante corrente, em ms de `k_uptime_get`.
 *
 * @retval 0       A faixa corrente já está gravada, ou acabou de ser.
 * @retval -EAGAIN Sem faixa, ou faixa nova, ainda no debounce.
 * @retval -EBUSY  Tocando: gravar agora atrasaria a próxima nota.
 * @retval -ENOENT A faixa não está na biblioteca publicada.
 * @retval -ESTALE O cartão foi trocado desde que a faixa foi escolhida: o
 *                 índice agora é de outra música. Não grava até a faixa
 *                 mudar.
 * @retval outro   Erro de `settings_save_one`; tenta de novo na próxima.
 */
int last_track_poll(int64_t now_ms);

/**
 * @brief Esquece o que está em RAM, como num boot sem nada gravado.
 *
 * Não apaga a flash. Existe para os testes simularem a reinicialização:
 * esquecer, chamar @ref last_track_init de novo e ver a faixa voltar da
 * flash.
 */
void last_track_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_LAST_TRACK_TEST_H_ */

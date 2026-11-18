/**
 * @file player.h
 * @brief Contrato: inicialização da máquina de reprodução.
 *
 * O player consome os comandos existentes em cmd.h e publica estado
 * por snapshot.h. Solicita faixas ao loader e aplica eventos por tone.h,
 * usando prazos absolutos. A máquina de estados e o timer serão internos.
 *
 * Não há uma segunda API pública de tocar/pausar: os comandos já
 * definidos são a entrada de controle. Arquivos, parser e gráficos não
 * são executados pela thread do player nem pelo callback do timer.
 *
 * Próxima/anterior circulam pela biblioteca informada na inicialização.
 */

#ifndef VITROLINHA_PLAYER_H_
#define VITROLINHA_PLAYER_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Prepara estado, timer e thread que consumirá a fila de comandos.
 * @param track_count Quantidade obtida por storage_scan, de 0 a LIBRARY_MAX.
 * @return 0 no sucesso ou erro negativo de inicialização.
 * @note Chamar uma vez, após loader_init() e tone_init(). O estado inicial
 *       é parado e sem faixa; fim, parada e falha silenciam a saída.
 *       A pausa preserva a nota corrente; a retomada reinicia essa nota.
 *       A quantidade da biblioteca fica fixa até encerrar a aplicação.
 */
int player_init(size_t track_count);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_PLAYER_H_ */

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
 * A biblioteca é a de library.h, lida sem trava a cada comando: seleção,
 * próxima e anterior só valem com ela em ::LIBRARY_READY, e próxima e
 * anterior pulam as faixas recusadas e dão a volta nas pontas.
 */

#ifndef VITROLINHA_PLAYER_H_
#define VITROLINHA_PLAYER_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Prepara estado, timer e thread que consumirá a fila de comandos.
 * @return 0 no sucesso ou erro negativo de inicialização.
 * @note Chamar uma vez, após loader_init() e tone_init(). O estado inicial
 *       é parado e sem faixa; fim, parada e falha silenciam a saída.
 *       A pausa preserva a nota corrente; a retomada reinicia essa nota.
 */
int player_init(void);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_PLAYER_H_ */

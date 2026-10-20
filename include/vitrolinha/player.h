/**
 * @file player.h
 * @brief Contrato: inicialização da máquina de reprodução.
 *
 * O player consumirá os comandos existentes em cmd.h e publicará estado
 * por snapshot.h. Solicitará faixas ao loader e aplicará eventos por tone.h,
 * usando prazos absolutos. A máquina de estados e o timer serão internos.
 *
 * Não haverá uma segunda API pública de tocar/pausar: os comandos já
 * definidos são a entrada de controle. Arquivos, parser e gráficos não
 * serão executados pela thread do player nem pelo callback do timer.
 *
 * Somente declaração; não inicia nenhuma reprodução nesta etapa.
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
 *       será parado e sem faixa; fim, parada e falha deverão silenciar.
 *       Pausa preservará a nota corrente e deslocará a base ao retomar,
 *       seguindo o contrato temporal do projeto.
 */
int player_init(void);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_PLAYER_H_ */

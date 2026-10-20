/**
 * @file player.c
 * @brief Esqueleto da máquina de estados e temporização da reprodução.
 *
 * Consumirá cmd.h, solicitará faixas ao loader, aplicará eventos por tone.h
 * e publicará snapshot.h. A thread e o callback do timer serão detalhes
 * internos deste módulo. Os prazos serão absolutos, calculados pela base
 * da faixa mais o offset de cada evento.
 *
 * Deverá tratar fim, pausa, parada, troca de faixa, respostas de cargas
 * canceladas e falta de eventos. Toda parada ou falha silenciará a saída.
 * A política de pausa reiniciará a nota corrente, deslocando a base temporal
 * e mantendo coerentes o timer e o snapshot.
 *
 * Somente assinatura pública; os comandos de controle continuam em cmd.h.
 * Não há thread, callback ou função implementada. Não integrado ao build.
 */

#include <vitrolinha/player.h>

/**
 * @brief A implementar: preparar a máquina de estados, timer e consumidor de comandos.
 * @see player.h para pré-condições e retorno da inicialização.
 */
int player_init(void);

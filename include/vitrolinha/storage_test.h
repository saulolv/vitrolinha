#ifndef STORAGE_TEST_H_
#define STORAGE_TEST_H_
/**
 * @file storage.h
 * @brief Contrato: varredura e carga da biblioteca.
 *
 * Isola a origem dos arquivos do resto do sistema. Nenhum outro módulo inclui
 * cabeçalho de sistema de arquivos, nem sabe que existe FAT, SPI ou
 * detecção de presença — o que é o que permite trocar a origem das faixas
 * (cartão, pasta do computador, dados embutidos no binário) sem tocar em quem
 * as consome.
 *
 * Montar o cartão **não** faz parte da interface, de propósito: a
 * implementação que fala com o hardware monta sozinha na inicialização, e o
 * chamador só descobre o resultado por @ref storage_present ou pelo código
 * de erro da varredura.
 *
 * @note Nada aqui pode ser chamado da thread `player` nem do callback do
 *       temporizador. É por isso que o arquivo é carregado inteiro antes de
 *       a reprodução começar: durante a música, este módulo não é tocado
 *       nenhuma vez.
 *
 * @see docs/especificacao-vitrolinha.md, seções 6.3 e 9
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <vitrolinha/track.h>

#ifdef __cplusplus
extern "C" {
#endif

int storage_init(void);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_TEST_H_

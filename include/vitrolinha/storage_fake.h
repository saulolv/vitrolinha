/**
 * @file storage_fake.h
 * @brief Painel de controle da implementação de mentira do @ref storage.h.
 *
 * A implementação de mentira devolve duas faixas embutidas no binário. Ela
 * existe por dois motivos:
 *
 * 1. Destravar as trilhas A e B antes de a E3 existir: dá para tocar e
 *    desenhar a biblioteca sem cartão nenhum na placa.
 * 2. Tornar testáveis os caminhos de falha do contrato. "Sem cartão" e
 *    "falha de leitura" não se reproduzem com hardware de verdade sem alguém
 *    puxar o cartão na hora certa.
 *
 * Este cabeçalho **não** faz parte do contrato: nenhum módulo de produção o
 * inclui. Some junto com a implementação de mentira, na E3.
 *
 * @see docs/especificacao-vitrolinha.md, seção 9
 */

#ifndef VITROLINHA_STORAGE_FAKE_H_
#define VITROLINHA_STORAGE_FAKE_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Quantidade de faixas embutidas no binário. */
#define STORAGE_FAKE_TRACK_COUNT 2U

/**
 * @brief Simula presença ou ausência de cartão.
 *
 * @param present Falso faz @ref storage_scan e @ref storage_load
 *                devolverem `-ENODEV` e @ref storage_present devolver falso.
 */
void storage_fake_set_present(bool present);

/**
 * @brief Simula um cartão presente que não responde.
 *
 * @param failing Verdadeiro faz @ref storage_scan e @ref storage_load
 *                devolverem `-EIO`, com o cartão ainda detectado.
 */
void storage_fake_set_failing(bool failing);

/**
 * @brief Devolve tudo ao estado inicial: cartão presente e saudável.
 *
 * Chamado no preparo de cada teste, para que um caso não herde o estado do
 * anterior.
 */
void storage_fake_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_STORAGE_FAKE_H_ */

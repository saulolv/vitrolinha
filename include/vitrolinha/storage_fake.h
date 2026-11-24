/**
 * @file storage_fake.h
 * @brief Painel de controle da implementação de mentira do @ref storage.h.
 *
 * A implementação de mentira devolve duas faixas embutidas no binário. Ela
 * existe por dois motivos:
 *
 * 1. Dar aos testes uma biblioteca sem cartão nem sistema de arquivos: dá
 *    para tocar e desenhar a biblioteca sem disco nenhum.
 * 2. Tornar testáveis os caminhos de falha do contrato. "Sem cartão" e
 *    "falha de leitura" não se reproduzem com hardware de verdade sem alguém
 *    puxar o cartão na hora certa.
 *
 * Este cabeçalho **não** faz parte do contrato: nenhum módulo de produção o
 * inclui. A implementação de mentira só entra no binário quando o
 * `CONFIG_VITROLINHA_STORAGE_FAKE` a escolhe — no simulador, e nos testes que
 * não enxergam o Kconfig do projeto.
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
 *                devolverem `-ENODEV`.
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
 * @brief Faz a próxima @ref storage_load devolver @p err.
 *
 * Só a próxima: a falha é consumida pela carga que a recebe. Serve para os
 * erros que são culpa do arquivo, e não do cartão — `-EFBIG` de um arquivo
 * grande, `-ENOENT` de um arquivo que sumiu —, que as faixas embutidas não
 * produzem por conta própria.
 *
 * @param err Código negativo a devolver, ou 0 para desarmar.
 */
void storage_fake_fail_next_load(int err);

/**
 * @brief Devolve tudo ao estado inicial: cartão presente e saudável, sem
 *        falha de carga armada.
 *
 * Chamado no preparo de cada teste, para que um caso não herde o estado do
 * anterior.
 */
void storage_fake_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_STORAGE_FAKE_H_ */

/**
 * @file bench.h
 * @brief Parâmetros da medição do RNF03, num lugar só.
 *
 * Tudo aqui é premissa declarada, não constante de conveniência: o orçamento
 * abaixo é a conta que decide se as faixas vizinhas precisam de pré-carga.
 */

#ifndef VITROLINHA_BENCH_H_
#define VITROLINHA_BENCH_H_

#include <vitrolinha/storage.h>
#include <vitrolinha/track.h>

/** @brief Ponto de montagem. `disk-name = "SD"` no nó `sdhc0` da placa. */
#define BENCH_MOUNT_POINT "/SD:"

/*
 * Os limites da biblioteca simulada vêm dos contratos da aplicação, e não de
 * números escritos aqui. Se o teto da biblioteca ou o do buffer de faixa
 * mudarem, a bancada passa a medir o novo pior caso sozinha — em vez de
 * continuar medindo o antigo e dizendo que está tudo bem.
 */

/** @brief Faixas na biblioteca simulada. */
#define BENCH_TRACKS LIBRARY_MAX

/** @brief Maior faixa: o teto do buffer estático da aplicação. */
#define BENCH_FILE_MAX STORAGE_FILE_MAX

/**
 * @brief Passo de tamanho entre faixas, em bytes.
 *
 * A i-ésima faixa tem `(i + 1) * BENCH_TRACK_STEP` bytes, então a última tem
 * exatamente ::BENCH_FILE_MAX. A régua cobre de propósito toda a escala: um
 * RTTTL típico tem de 100 a 400 bytes, e o teto é o maior arquivo que a
 * aplicação aceita — o pior caso de leitura possível.
 */
#define BENCH_TRACK_STEP (BENCH_FILE_MAX / BENCH_TRACKS)

/** @brief Bytes de cabeçalho lidos na varredura, para extrair o nome RTTTL. */
#define BENCH_HEADER_BYTES 64U

/* ---------------------------------------------------------------------------
 * O orçamento do RNF03
 *
 * "Latência botão -> efeito audível: menor que 100 ms, incluindo seguinte e
 * anterior." O caminho tem quatro trechos, e o armazenamento é só um deles:
 *
 *   1. Antirrebote do botão      20 ms, fixados por `debounce-interval-ms`
 *                                no nó `gpio-keys` do overlay
 *   2. Fila de comandos          desprezível: `k_msgq` sem bloqueio
 *   3. Abrir e ler o arquivo     É O QUE ESTA BANCADA MEDE
 *   4. Interpretar e soar        reserva; medida na issue #10
 *
 * A reserva do trecho 4 é premissa, não medição — o interpretador ainda não
 * existe. Está declarada aqui para que o orçamento do armazenamento seja um
 * número explícito, e para que revisá-la seja mexer numa linha.
 * -------------------------------------------------------------------------*/

/** @brief O requisito, em microssegundos. */
#define BENCH_RNF03_TOTAL_US 100000U

/** @brief `debounce-interval-ms = <20>`, nos nós de botão do overlay. */
#define BENCH_DEBOUNCE_US 20000U

/** @brief Reserva para interpretar o arquivo e disparar a primeira nota. */
#define BENCH_PARSER_RESERVE_US 20000U

/** @brief O que sobra para abrir e ler. */
#define BENCH_STORAGE_BUDGET_US \
	(BENCH_RNF03_TOTAL_US - BENCH_DEBOUNCE_US - BENCH_PARSER_RESERVE_US)

#endif /* VITROLINHA_BENCH_H_ */

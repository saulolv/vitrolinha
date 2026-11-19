/**
 * @file playback_trace.h
 * @brief Contrato: registro dos tons efetivamente aplicados no simulador.
 *
 * Guarda instantes observados e frequências em um buffer de capacidade
 * fixa. A gravação em memória é permitida no callback do timer; a
 * exportação para arquivo ocorre somente depois de encerrar a captura.
 *
 * CSV de saída: cabeçalho elapsed_us,frequency_hz. Cada linha
 * informa quando a frequência passou a valer. A última linha é silêncio
 * no instante de encerramento, inclusive se o tom anterior já era zero.
 * Este formato é distinto do CSV musical de frequência e duração.
 *
 * Use k_cyc_to_us_floor64(k_cycle_get_64()) para begin/end: é o mesmo
 * relógio observado por tone_trace, inclusive dentro do callback do timer.
 */

#ifndef VITROLINHA_PLAYBACK_TRACE_H_
#define VITROLINHA_PLAYBACK_TRACE_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Limite fixo por captura, incluindo pausas e silêncio de encerramento. */
#define PLAYBACK_TRACE_CAPACITY 2048U

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Uma mudança de tom observada durante a execução. */
struct playback_trace_entry {
	/** @brief Tempo desde begin, incluindo pausas do usuário. */
	uint64_t elapsed_us;
	/** @brief Frequência aplicada; zero indica silêncio. */
	uint32_t freq_hz;
};

/**
 * @brief Inicia uma captura, descartando o registro encerrado anterior.
 * @param start_us Instante absoluto no relógio simulado do Zephyr.
 * @return 0 no sucesso ou -EBUSY se já houver uma captura aberta.
 * @note Chamar em thread, com saída silenciosa e timer parado. O estado
 *       inicial é silêncio; a primeira mudança pode ocorrer após start_us.
 */
int playback_trace_begin(uint64_t start_us);

/**
 * @brief Registra uma aplicação de frequência em memória, sem bloquear.
 * @param timestamp_us Instante absoluto na mesma base de begin.
 * @param frequency_hz Frequência aplicada, inclusive zero para silêncio.
 * @return 0 no sucesso; -EACCES sem captura; -EINVAL para tempo anterior
 *         ao início ou ao último registro; -ENOSPC se o buffer esgotar.
 * @note Produtores devem ser serializados pelo lado do player. Estouro
 *       marca a captura incompleta; nunca sobrescrever eventos antigos.
 */
int playback_trace_record(uint64_t timestamp_us, uint32_t frequency_hz);

/**
 * @brief Registra o silêncio final e encerra a captura.
 * @param timestamp_us Instante absoluto, não anterior ao último registro.
 * @return 0 no sucesso; -ENOSPC se incompleta; -EACCES sem captura aberta;
 *         -EINVAL para instante inválido, mantendo a captura aberta.
 * @note Chamar em thread após parar o timer e silenciar a saída. A captura
 *       fecha mesmo com -ENOSPC, mas sua exportação será recusada.
 */
int playback_trace_end(uint64_t timestamp_us);

/**
 * @brief Consulta um evento de uma captura encerrada, sem acesso a arquivo.
 * @param index Posição a partir de zero.
 * @param out Registro de destino; válido somente no retorno 1.
 * @return 1 para registro, 0 após o último, -EBUSY durante captura,
 *         -ENOSPC para captura incompleta ou -EINVAL para argumento inválido.
 * @note Não executar simultaneamente com begin; útil para inspeção e testes.
 */
int playback_trace_read(size_t index, struct playback_trace_entry *out);

/**
 * @brief Exporta uma captura completa e encerrada para CSV no computador.
 * @param path Caminho de destino, terminado em NUL; substitui arquivo existente.
 * @return 0 no sucesso; -EBUSY durante captura; -ENOSPC se incompleta;
 *         -EINVAL para argumento inválido; -EIO para erro de escrita.
 * @note Exclusivo do simulador, em thread de baixa prioridade. Nunca
 *       chamar do timer ou da thread do player; não concorrer com begin.
 */
int playback_trace_export_csv(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_PLAYBACK_TRACE_H_ */

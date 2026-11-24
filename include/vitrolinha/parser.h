/**
 * @file parser.h
 * @brief Contrato: a faixa carregada, em qualquer dos dois formatos.
 *
 * O `loader` não sabe qual formato tem nas mãos. O formato sai do
 * conteúdo, e não da extensão: o CSV começa sempre pelo cabeçalho
 * `frequency_hz,duration_ms`, e todo o resto é tratado como RTTTL. Assim a
 * carga não precisa carregar o nome do arquivo junto com os bytes.
 *
 * - RTTTL (@ref parser_rtttl.h): o formato das músicas.
 * - CSV (@ref parser_csv.h): o de bancada e do arquivo de calibração.
 *
 * As três operações repassam ao parser do formato, com os mesmos retornos.
 */

#ifndef VITROLINHA_PARSER_H_
#define VITROLINHA_PARSER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <vitrolinha/event.h>
#include <vitrolinha/parser_csv.h>
#include <vitrolinha/parser_rtttl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Uma faixa carregada, de um dos dois formatos. */
struct parser {
	/** @brief Verdadeiro para RTTTL, falso para CSV. */
	bool rtttl;
	union {
		struct parser_csv csv;
		struct parser_rtttl rtttl;
	} format;
};

/**
 * @brief Reconhece o formato, valida a faixa inteira e prepara o iterador.
 *
 * O RTTTL é transposto pelo limiar `CONFIG_VITROLINHA_TRANSPOSE_MIN_HZ`; o
 * CSV nunca, porque o arquivo de calibração tem de tocar o que diz.
 *
 * @return Os de @ref parser_csv_init ou de @ref parser_rtttl_init.
 */
int parser_init(struct parser *parser, const uint8_t *buffer, size_t length,
		struct parser_error *error);

/** @brief Próximo evento; os retornos de @ref parser_csv_next. */
int parser_next(struct parser *parser, struct playback_event *out);

/** @brief Volta ao primeiro evento; os retornos de @ref parser_csv_rewind. */
int parser_rewind(struct parser *parser);

/** @brief Duração da faixa validada, em µs: o instante do silêncio final. */
uint32_t parser_total_us(const struct parser *parser);

/** @brief O silêncio final já foi entregue? */
bool parser_finished(const struct parser *parser);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_PARSER_H_ */

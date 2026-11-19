/**
 * @file parser.c
 * @brief Implementação de @ref parser.h.
 */

#include <vitrolinha/parser.h>

#include <errno.h>
#include <string.h>

/*
 * Os testes que não declaram as opções da aplicação compilam o parser sem
 * elas; aí a transposição fica desligada, como no padrão.
 */
#ifndef CONFIG_VITROLINHA_TRANSPOSE_MIN_HZ
#define CONFIG_VITROLINHA_TRANSPOSE_MIN_HZ 0
#endif

/** @brief A primeira linha de todo CSV de faixa. */
static const char csv_header[] = "frequency_hz,duration_ms";

/** @brief O buffer começa pelo cabeçalho do CSV? */
static bool is_csv(const uint8_t *buffer, size_t length)
{
	return (length >= (sizeof(csv_header) - 1U)) &&
	       (memcmp(buffer, csv_header, sizeof(csv_header) - 1U) == 0);
}

int parser_init(struct parser *parser, const uint8_t *buffer, size_t length,
		struct parser_csv_error *error)
{
	if (parser == NULL) {
		/* Os dois parsers recusam do mesmo jeito; o CSV também zera o erro. */
		return parser_csv_init(NULL, buffer, length, error);
	}

	parser->rtttl = (buffer != NULL) && !is_csv(buffer, length);
	if (parser->rtttl) {
		return parser_rtttl_init(&parser->format.rtttl, buffer, length, error,
					 CONFIG_VITROLINHA_TRANSPOSE_MIN_HZ);
	}

	return parser_csv_init(&parser->format.csv, buffer, length, error);
}

int parser_next(struct parser *parser, struct playback_event *out)
{
	if (parser == NULL) {
		return -EINVAL;
	}

	return parser->rtttl ? parser_rtttl_next(&parser->format.rtttl, out)
			     : parser_csv_next(&parser->format.csv, out);
}

int parser_rewind(struct parser *parser)
{
	if (parser == NULL) {
		return -EINVAL;
	}

	return parser->rtttl ? parser_rtttl_rewind(&parser->format.rtttl)
			     : parser_csv_rewind(&parser->format.csv);
}

uint32_t parser_total_us(const struct parser *parser)
{
	return parser->rtttl ? parser->format.rtttl.total_us : parser->format.csv.total_us;
}

bool parser_finished(const struct parser *parser)
{
	return parser->rtttl ? parser->format.rtttl.final_event_emitted
			     : parser->format.csv.final_event_emitted;
}

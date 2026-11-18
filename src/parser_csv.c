/**
 * @file parser_csv.c
 * @brief Parser de pares frequência/duração em CSV, sem E/S ou alocação.
 *
 * A decodificação prepara todos os eventos em uma única passagem. A API de
 * iteração continua disponível para consumidores que precisem dela.
 * A iteração relê o mesmo buffer imutável e calcula os instantes pela soma
 * inteira das durações. O último evento silencia no fim da faixa.
 */

#include <vitrolinha/parser_csv.h>

#include <errno.h>

/** @brief Uma linha delimitada por tamanho, sem incluir LF ou CRLF. */
struct csv_line {
	const uint8_t *data;
	size_t length;
};

/** @brief Preenche o diagnóstico opcional e devolve o código de erro. */
static int fail(struct parser_csv_error *error, size_t line, size_t column, int code)
{
	if (error != NULL) {
		error->line = line;
		error->column = column;
	}

	return code;
}

/**
 * @brief Recorta a linha atual e avança position, sem ler além de length.
 * @note Um CR só é removido se imediatamente seguido de LF. Nos demais
 *       casos permanece no campo e será rejeitado pela validação.
 */
static struct csv_line read_line(const uint8_t *buffer, size_t length, size_t *position)
{
	size_t start = *position;

	while ((*position < length) && (buffer[*position] != '\n')) {
		(*position)++;
	}

	struct csv_line line = {
		.data = buffer + start,
		.length = *position - start,
	};

	if (*position < length) {
		(*position)++;
		if ((line.length > 0U) && (line.data[line.length - 1U] == '\r')) {
			line.length--;
		}
	}

	return line;
}

/**
 * @brief Lê um inteiro decimal sem sinal, verificando overflow antes da conta.
 * @note Deixa column no separador ou no fim. Espaços, sinais e frações não
 *       são aceitos; todos os acessos respeitam o tamanho da linha.
 */
static int read_uint32(const struct csv_line *line, size_t *column, uint32_t *out,
		       size_t line_number, struct parser_csv_error *error)
{
	size_t start = *column;
	uint32_t value = 0U;

	while (*column < line->length) {
		uint8_t byte = line->data[*column];

		if ((byte < '0') || (byte > '9')) {
			break;
		}

		uint32_t digit = byte - '0';

		if (value > (UINT32_MAX - digit) / 10U) {
			return fail(error, line_number, *column + 1U, -ERANGE);
		}

		value = value * 10U + digit;
		(*column)++;
	}

	if (*column == start) {
		return fail(error, line_number, start + 1U, -EINVAL);
	}

	*out = value;
	return 0;
}

/**
 * @brief Lê uma linha musical e avança o cursor somente em caso de sucesso.
 * @note Compartilhado pela validação integral e pela iteração. O acumulador
 *       temporário de 64 bits impede overflow na conversão de ms para us.
 */
static int read_event(struct parser_csv *parser, struct playback_event *out,
		      struct parser_csv_error *error)
{
	size_t position = parser->position;
	struct csv_line line = read_line(parser->buffer, parser->length, &position);
	size_t column = 0U;
	uint32_t frequency;
	uint32_t duration;
	int err = read_uint32(&line, &column, &frequency, parser->line, error);

	if (err != 0) {
		return err;
	}

	if ((column == line.length) || (line.data[column] != ',')) {
		return fail(error, parser->line, column + 1U, -EINVAL);
	}

	column++;
	size_t duration_column = column + 1U;

	err = read_uint32(&line, &column, &duration, parser->line, error);
	if (err != 0) {
		return err;
	}

	if (column != line.length) {
		return fail(error, parser->line, column + 1U, -EINVAL);
	}

	if (duration == 0U) {
		return fail(error, parser->line, duration_column, -EINVAL);
	}

	uint64_t next_offset = (uint64_t)parser->offset_us + (uint64_t)duration * 1000U;

	if (next_offset > UINT32_MAX) {
		return fail(error, parser->line, duration_column, -ERANGE);
	}

	out->offset_us = parser->offset_us;
	out->freq_hz = frequency;
	parser->offset_us = (uint32_t)next_offset;
	parser->position = position;
	parser->line++;
	return 1;
}

static int parse_csv(struct parser_csv *parser, const uint8_t *buffer,
		     size_t length, struct playback_event *events, size_t capacity,
		     struct parser_csv_error *error)
{
	static const char header[] = "frequency_hz,duration_ms";

	if (error != NULL) {
		*error = (struct parser_csv_error){0};
	}

	if (parser == NULL) {
		return -EINVAL;
	}

	/* Uma inicialização que falha não deixa a faixa anterior utilizável. */
	*parser = (struct parser_csv){0};
	if ((buffer == NULL) || (length == 0U)) {
		return -EINVAL;
	}

	struct parser_csv candidate = {
		.buffer = buffer,
		.length = length,
		.line = 2U,
	};
	struct csv_line first = read_line(buffer, length, &candidate.position);

	for (size_t i = 0U; i < sizeof(header) - 1U; i++) {
		if ((i >= first.length) || (first.data[i] != (uint8_t)header[i])) {
			return fail(error, 1U, i + 1U, -EINVAL);
		}
	}

	if (first.length != sizeof(header) - 1U) {
		return fail(error, 1U, sizeof(header), -EINVAL);
	}

	while (candidate.position < length) {
		struct playback_event event;
		int err = read_event(&candidate, &event, error);

		if (err < 0) {
			return err;
		}
		if (events != NULL) {
			/* Reserva também uma posição para o silêncio final. */
			if (candidate.row_count + 1U >= capacity) {
				return fail(error, candidate.line - 1U, 1U, -ENOSPC);
			}
			events[candidate.row_count] = event;
		}
		candidate.row_count++;
	}

	if (candidate.row_count == 0U) {
		return fail(error, 2U, 1U, -EINVAL);
	}

	candidate.total_us = candidate.offset_us;
	if (events != NULL) {
		events[candidate.row_count] = (struct playback_event){
			.offset_us = candidate.total_us, .freq_hz = 0U,
		};
	}
	(void)parser_csv_rewind(&candidate);
	*parser = candidate;
	return 0;
}

int parser_csv_init(struct parser_csv *parser, const uint8_t *buffer,
		    size_t length, struct parser_csv_error *error)
{
	return parse_csv(parser, buffer, length, NULL, 0, error);
}

int parser_csv_decode(const uint8_t *buffer, size_t length,
		      struct playback_event *events, size_t capacity,
		      size_t *count, uint32_t *total_us, struct parser_csv_error *error)
{
	if (events == NULL || count == NULL || total_us == NULL) {
		return -EINVAL;
	}
	*count = 0;
	*total_us = 0;
	struct parser_csv parser;
	int err = parse_csv(&parser, buffer, length, events, capacity, error);

	if (err == 0) {
		*count = parser.row_count + 1U;
		*total_us = parser.total_us;
	}
	return err;
}

int parser_csv_next(struct parser_csv *parser, struct playback_event *out)
{
	if ((parser == NULL) || (out == NULL)) {
		return -EINVAL;
	}
	if (parser->buffer == NULL) {
		return -EINVAL;
	}
	if (parser->final_event_emitted) {
		return 0;
	}

	if (parser->position == parser->length) {
		*out = (struct playback_event){.offset_us = parser->total_us, .freq_hz = 0U};
		parser->final_event_emitted = true;
		return 1;
	}

	return read_event(parser, out, NULL);
}

int parser_csv_rewind(struct parser_csv *parser)
{
	if (parser == NULL) {
		return -EINVAL;
	}
	if (parser->buffer == NULL) {
		return -EINVAL;
	}

	size_t position = 0U;

	(void)read_line(parser->buffer, parser->length, &position);
	parser->position = position;
	parser->line = 2U;
	parser->offset_us = 0U;
	parser->final_event_emitted = false;
	return 0;
}

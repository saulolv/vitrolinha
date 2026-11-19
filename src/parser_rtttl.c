/**
 * @file parser_rtttl.c
 * @brief Implementação de @ref parser_rtttl.h, escrita do zero.
 *
 * A validação percorre a faixa inteira uma vez, com a mesma função que a
 * iteração usa depois para ler cada nota. Assim a iteração não tem erro de
 * conteúdo a tratar: o buffer é imutável entre o init e o fim da faixa.
 */

#include <vitrolinha/parser_rtttl.h>
#include <vitrolinha/tone.h>

#include <errno.h>
#include <limits.h>

#include <zephyr/sys/util.h>

/** @brief Padrões do formato, para quando o cabeçalho os omite. */
#define DEFAULT_DURATION 4U
#define DEFAULT_OCTAVE   6U
#define DEFAULT_BPM      63U

/** @brief Faixa aceita de oitavas: de Dó 1 (32,7 Hz) a Si 8 (7,9 kHz). */
#define OCTAVE_MIN 1U
#define OCTAVE_MAX 8U

/** @brief Maior andamento do formato. */
#define BPM_MAX 900U

/**
 * @brief Microssegundos por sexagésima-quarta, vezes o andamento.
 *
 * Uma semínima dura 60 s / bpm, e vale 16 sexagésimas-quartas: cada uma dura
 * 60e6 / (16 * bpm) µs = 3 750 000 / bpm µs.
 */
#define US_PER_N64_TIMES_BPM 3750000ULL

/** @brief Teto do silêncio de articulação no fim de cada nota. */
#define ARTICULATION_MAX_US 20000U

/** @brief Valor acima de qualquer campo válido; satura a leitura de números. */
#define NUMBER_CAP 10000U

/**
 * @brief Frequências da oitava 8, em mHz, de Dó a Si.
 *
 * Temperamento igual com Lá 4 em 440 Hz: `440 * 2^((n - 57) / 12)` para o
 * semitom `n` a partir do Dó 0. A oitava mais aguda guarda a maior precisão;
 * as de baixo saem por divisão, arredondada, por potência de dois.
 */
static const uint32_t octave8_mhz[12] = {
	4186009, 4434922, 4698636, 4978032, 5274041, 5587652,
	5919911, 6271927, 6644875, 7040000, 7458620, 7902133,
};

/** @brief Semitom de cada letra, de `a` a `h`, a partir do Dó. */
static const int8_t letter_semitone[] = {
	9,  /* a */
	11, /* b */
	0,  /* c */
	2,  /* d */
	4,  /* e */
	5,  /* f */
	7,  /* g */
	11, /* h: o Si na grafia alemã */
};

/** @brief Uma nota ou pausa já lida. */
struct note {
	/** @brief Duração em sexagésimas-quartas. */
	uint32_t n64;
	/** @brief Semitons a partir do Dó 0; sem sentido para pausa. */
	unsigned int pitch;
	bool pause;
};

uint32_t parser_rtttl_frequency(unsigned int pitch)
{
	unsigned int octave = pitch / 12U;
	uint64_t mhz = octave8_mhz[pitch % 12U];

	if (octave > 11U) {
		return 0U;
	}

	if (octave >= 8U) {
		mhz <<= (octave - 8U);
		return (uint32_t)((mhz + 500U) / 1000U);
	}

	uint64_t divisor = 1000ULL << (8U - octave);

	return (uint32_t)((mhz + (divisor / 2U)) / divisor);
}

/**
 * @brief A frequência da nota existe e não passa do que a saída toca.
 *
 * Uma comparação cobre os dois lados: a frequência zero, de nota fora da
 * tabela, dá a volta na subtração e passa do teto.
 */
static bool pitch_fits(unsigned int pitch)
{
	return (parser_rtttl_frequency(pitch) - 1U) < TONE_FREQ_MAX_HZ;
}

static bool is_space(uint8_t byte)
{
	return (byte == ' ') || (byte == '\t') || (byte == '\r') || (byte == '\n');
}

static bool is_digit(uint8_t byte)
{
	return (byte >= '0') && (byte <= '9');
}

static uint8_t lower(uint8_t byte)
{
	return ((byte >= 'A') && (byte <= 'Z')) ? (uint8_t)(byte + ('a' - 'A')) : byte;
}

/** @brief O byte em @p pos, ou 0 no fim do buffer: nenhum campo usa NUL. */
static uint8_t peek(const uint8_t *buffer, size_t length, size_t pos)
{
	return (pos < length) ? buffer[pos] : 0U;
}

static void skip_space(const uint8_t *buffer, size_t length, size_t *pos)
{
	while ((*pos < length) && is_space(buffer[*pos])) {
		(*pos)++;
	}
}

/**
 * @brief Lê um número decimal sem sinal.
 *
 * Satura em ::NUMBER_CAP, acima de qualquer valor válido, para que um número
 * comprido seja recusado pela faixa e não dê a volta.
 *
 * @return Quantos dígitos foram lidos; zero se não havia número.
 */
static size_t read_number(const uint8_t *buffer, size_t length, size_t *pos, uint32_t *out)
{
	size_t start = *pos;
	uint32_t value = 0U;

	while ((*pos < length) && is_digit(buffer[*pos])) {
		if (value < NUMBER_CAP) {
			value = (value * 10U) + (uint32_t)(buffer[*pos] - '0');
		}
		(*pos)++;
	}

	*out = value;
	return *pos - start;
}

/** @brief Durações do formato: as potências de dois de 1 a 32. */
static bool duration_valid(uint32_t duration)
{
	return (duration >= 1U) && (duration <= 32U) && ((duration & (duration - 1U)) == 0U);
}

/** @brief Converte a posição do erro em linha e coluna, e devolve @p code. */
static int fail(const uint8_t *buffer, size_t pos, struct parser_csv_error *error, int code)
{
	if (error != NULL) {
		size_t line = 1U;
		size_t line_start = 0U;

		for (size_t i = 0U; i < pos; i++) {
			if (buffer[i] == '\n') {
				line++;
				line_start = i + 1U;
			}
		}

		error->line = line;
		error->column = pos - line_start + 1U;
	}

	return code;
}

/**
 * @brief Lê o cabeçalho: nome, padrões e o segundo `:`.
 *
 * O nome é ignorado aqui — quem o mostra é a varredura da biblioteca —, mas
 * não pode atravessar uma quebra de linha: sem `:` na primeira linha, o
 * arquivo não é RTTTL.
 */
static int read_header(struct parser_rtttl *parser, struct parser_csv_error *error)
{
	const uint8_t *buffer = parser->buffer;
	size_t length = parser->length;
	size_t pos = 0U;

	while (peek(buffer, length, pos) != ':') {
		if ((pos == length) || (buffer[pos] == '\n')) {
			return fail(buffer, pos, error, -EINVAL);
		}
		pos++;
	}
	pos++;

	for (;;) {
		skip_space(buffer, length, &pos);
		if (peek(buffer, length, pos) == ':') {
			break;
		}

		size_t key_pos = pos;
		uint8_t key = lower(peek(buffer, length, pos));
		uint32_t value;

		if ((key != 'd') && (key != 'o') && (key != 'b')) {
			return fail(buffer, key_pos, error, -EINVAL);
		}
		pos++;
		skip_space(buffer, length, &pos);
		if (peek(buffer, length, pos) != '=') {
			return fail(buffer, pos, error, -EINVAL);
		}
		pos++;
		skip_space(buffer, length, &pos);

		size_t value_pos = pos;

		if (read_number(buffer, length, &pos, &value) == 0U) {
			return fail(buffer, value_pos, error, -EINVAL);
		}

		if (key == 'd') {
			if (!duration_valid(value)) {
				return fail(buffer, value_pos, error, -EINVAL);
			}
			parser->default_duration = (uint8_t)value;
		} else if (key == 'o') {
			if ((value < OCTAVE_MIN) || (value > OCTAVE_MAX)) {
				return fail(buffer, value_pos, error, -ERANGE);
			}
			parser->default_octave = (uint8_t)value;
		} else {
			if ((value == 0U) || (value > BPM_MAX)) {
				return fail(buffer, value_pos, error, -ERANGE);
			}
			parser->bpm = (uint16_t)value;
		}

		skip_space(buffer, length, &pos);
		if (peek(buffer, length, pos) == ',') {
			pos++;
		} else if (peek(buffer, length, pos) != ':') {
			return fail(buffer, pos, error, -EINVAL);
		}
	}

	parser->notes_start = pos + 1U;
	return 0;
}

/**
 * @brief Lê a nota em @p pos e o separador depois dela.
 *
 * Usada pela validação, que repassa o erro, e pela iteração, que lê um
 * buffer já validado e por isso ignora o retorno.
 *
 * @param pos Entra no começo da nota (espaços antes são aceitos); sai no
 *            começo da próxima, ou no fim do buffer.
 */
static int read_note(const struct parser_rtttl *parser, size_t *pos, struct note *out,
		     struct parser_csv_error *error)
{
	const uint8_t *buffer = parser->buffer;
	size_t length = parser->length;
	uint32_t duration = parser->default_duration;
	uint32_t octave = parser->default_octave;
	bool dotted = false;

	skip_space(buffer, length, pos);

	size_t duration_pos = *pos;

	if (read_number(buffer, length, pos, &duration) > 0U) {
		if (!duration_valid(duration)) {
			return fail(buffer, duration_pos, error, -EINVAL);
		}
	} else {
		duration = parser->default_duration;
	}

	uint8_t letter = lower(peek(buffer, length, *pos));

	if (letter == 'p') {
		out->pause = true;
		out->pitch = 0U;
	} else if ((letter >= 'a') && (letter <= 'h')) {
		out->pause = false;
		out->pitch = (unsigned int)letter_semitone[letter - 'a'];
	} else {
		return fail(buffer, *pos, error, -EINVAL);
	}
	(*pos)++;

	if (peek(buffer, length, *pos) == '#') {
		if (out->pause) {
			return fail(buffer, *pos, error, -EINVAL);
		}
		out->pitch++;
		(*pos)++;
	}

	/* O ponto aparece antes ou depois da oitava, conforme quem escreveu o
	 * arquivo; as duas grafias valem, uma vez só.
	 */
	if (peek(buffer, length, *pos) == '.') {
		dotted = true;
		(*pos)++;
	}

	if (is_digit(peek(buffer, length, *pos))) {
		octave = (uint32_t)(buffer[*pos] - '0');
		if ((octave < OCTAVE_MIN) || (octave > OCTAVE_MAX)) {
			return fail(buffer, *pos, error, -ERANGE);
		}
		(*pos)++;
	}

	if (peek(buffer, length, *pos) == '.') {
		if (dotted) {
			return fail(buffer, *pos, error, -EINVAL);
		}
		dotted = true;
		(*pos)++;
	}

	skip_space(buffer, length, pos);
	if (*pos < length) {
		if (buffer[*pos] != ',') {
			return fail(buffer, *pos, error, -EINVAL);
		}
		(*pos)++;
		skip_space(buffer, length, pos);
		if (*pos == length) {
			/* Vírgula no fim: falta a nota que ela anuncia. */
			return fail(buffer, *pos, error, -EINVAL);
		}
	}

	/* 64/d e 96/d são inteiros para toda duração de 1 a 32. */
	out->n64 = (dotted ? 96U : 64U) / duration;
	out->pitch += octave * 12U;
	return 0;
}

/** @brief Instante, em µs, de @p n64 sexagésimas-quartas no andamento da faixa. */
static uint64_t n64_to_us(const struct parser_rtttl *parser, uint32_t n64)
{
	return ((uint64_t)n64 * US_PER_N64_TIMES_BPM) / parser->bpm;
}

int parser_rtttl_rewind(struct parser_rtttl *parser)
{
	if ((parser == NULL) || (parser->buffer == NULL)) {
		return -EINVAL;
	}

	parser->position = parser->notes_start;
	parser->n64 = 0U;
	parser->pending_silence = false;
	parser->last_silent = false;
	parser->final_event_emitted = false;
	return 0;
}

int parser_rtttl_init(struct parser_rtttl *parser, const uint8_t *buffer, size_t length,
		      struct parser_csv_error *error, uint32_t transpose_min_hz)
{
	if (error != NULL) {
		*error = (struct parser_csv_error){0};
	}

	if (parser == NULL) {
		return -EINVAL;
	}

	/* Uma inicialização que falha não deixa a faixa anterior utilizável. */
	*parser = (struct parser_rtttl){0};
	if ((buffer == NULL) || (length == 0U)) {
		return -EINVAL;
	}

	struct parser_rtttl candidate = {
		.buffer = buffer,
		.length = length,
		.default_duration = DEFAULT_DURATION,
		.default_octave = DEFAULT_OCTAVE,
		.bpm = DEFAULT_BPM,
	};
	int err = read_header(&candidate, error);

	if (err != 0) {
		return err;
	}

	size_t pos = candidate.notes_start;
	unsigned int lowest = UINT_MAX;
	unsigned int highest = 0U;

	skip_space(buffer, length, &pos);
	if (pos == length) {
		return fail(buffer, pos, error, -EINVAL);
	}

	while (pos < length) {
		size_t note_pos = pos;
		struct note note;

		err = read_note(&candidate, &pos, &note, error);
		if (err != 0) {
			return err;
		}

		candidate.n64 += note.n64;
		if (n64_to_us(&candidate, candidate.n64) > UINT32_MAX) {
			return fail(buffer, note_pos, error, -ERANGE);
		}

		if (!note.pause) {
			lowest = MIN(lowest, note.pitch);
			highest = MAX(highest, note.pitch);
		}
		candidate.note_count++;
	}

	/*
	 * Com oitavas de 1 a 8, toda nota cai entre 32 Hz (Dó 1) e 8,4 kHz (Si
	 * sustenido 8), dentro do que a saída toca; um teste confere a faixa
	 * inteira. A transposição sobe a faixa enquanto a nota mais grave estiver
	 * abaixo do limiar e a mais aguda ainda couber uma oitava acima.
	 */
	if ((transpose_min_hz > 0U) && (highest > 0U)) {
		while ((parser_rtttl_frequency(lowest + (12U * candidate.shift)) <
			transpose_min_hz) &&
		       pitch_fits(highest + (12U * (candidate.shift + 1U)))) {
			candidate.shift++;
		}
	}

	candidate.total_us = (uint32_t)n64_to_us(&candidate, candidate.n64);
	(void)parser_rtttl_rewind(&candidate);
	*parser = candidate;
	return 0;
}

int parser_rtttl_next(struct parser_rtttl *parser, struct playback_event *out)
{
	if ((parser == NULL) || (out == NULL) || (parser->buffer == NULL)) {
		return -EINVAL;
	}

	if (parser->final_event_emitted) {
		return 0;
	}

	if (parser->pending_silence) {
		*out = (struct playback_event){.offset_us = parser->pending_offset_us};
		parser->pending_silence = false;
		parser->last_silent = true;
		return 1;
	}

	while (parser->position < parser->length) {
		struct note note;

		/* Validado no init: a leitura não falha. */
		(void)read_note(parser, &parser->position, &note, NULL);

		uint32_t start_us = (uint32_t)n64_to_us(parser, parser->n64);

		parser->n64 += note.n64;

		uint32_t end_us = (uint32_t)n64_to_us(parser, parser->n64);

		if (note.pause) {
			if (parser->last_silent) {
				continue;
			}
			*out = (struct playback_event){.offset_us = start_us};
			parser->last_silent = true;
			return 1;
		}

		*out = (struct playback_event){
			.offset_us = start_us,
			.freq_hz = parser_rtttl_frequency(note.pitch + (12U * parser->shift)),
		};
		parser->pending_silence = true;
		parser->pending_offset_us =
			end_us - MIN((end_us - start_us) / 10U, ARTICULATION_MAX_US);
		parser->last_silent = false;
		return 1;
	}

	*out = (struct playback_event){.offset_us = parser->total_us};
	parser->final_event_emitted = true;
	return 1;
}

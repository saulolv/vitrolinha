/**
 * @file main.c
 * @brief Testes do parser RTTTL e da fachada que escolhe o formato.
 *
 * Verifica a afinação, os arquivos de exemplo nota a nota, a grade inteira
 * (o erro de tempo não acumula), a articulação, as pausas, a posição de cada
 * erro e a transposição. Não depende de E/S, timer ou buzzer.
 */

#include <vitrolinha/parser.h>
#include <vitrolinha/parser_rtttl.h>
#include <vitrolinha/tone.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/ztest.h>

static const uint8_t furelise[] = {
#include "furelise.rtx.inc"
};
static const uint8_t odetojoy[] = {
#include "odetojoy.rtx.inc"
};
static const uint8_t escala[] = {
#include "escala.csv.inc"
};

/** @brief Semitons a partir do Dó 0. */
#define PITCH(octave, semitone) (((octave) * 12U) + (semitone))

/** @brief µs de @p n64 sexagésimas-quartas a @p bpm, pela conta do parser. */
static uint32_t at(uint32_t n64, uint32_t bpm)
{
	return (uint32_t)(((uint64_t)n64 * 3750000ULL) / bpm);
}

/** @brief O silêncio de articulação de uma fatia de @p start a @p end µs. */
static uint32_t articulation(uint32_t start, uint32_t end)
{
	return end - MIN((end - start) / 10U, 20000U);
}

static int init(struct parser_rtttl *parser, const char *text)
{
	return parser_rtttl_init(parser, (const uint8_t *)text, strlen(text), NULL, 0U);
}

static void expect_event(struct parser_rtttl *parser, uint32_t offset, uint32_t frequency)
{
	struct playback_event event;

	zassert_equal(1, parser_rtttl_next(parser, &event));
	zassert_equal(offset, event.offset_us, "esperava %u us, veio %u", offset,
		      event.offset_us);
	zassert_equal(frequency, event.freq_hz, "esperava %u Hz, veio %u", frequency,
		      event.freq_hz);
}

/** @brief Uma nota tocada: o tom no início da fatia e o silêncio no fim. */
static void expect_note(struct parser_rtttl *parser, uint32_t start, uint32_t end,
			uint32_t frequency)
{
	expect_event(parser, start, frequency);
	expect_event(parser, articulation(start, end), 0U);
}

static void expect_end(struct parser_rtttl *parser)
{
	struct playback_event event = {.offset_us = 123U, .freq_hz = 456U};

	expect_event(parser, parser->total_us, 0U);
	zassert_equal(0, parser_rtttl_next(parser, &event));
	zassert_equal(0, parser_rtttl_next(parser, &event));
	zassert_equal(123U, event.offset_us, "o fim nao escreve no evento");
}

ZTEST_SUITE(parser_rtttl, NULL, NULL, NULL, NULL, NULL);

/* -------------------------------------------------------------------------- */
/* Afinação                                                                   */
/* -------------------------------------------------------------------------- */

ZTEST(parser_rtttl, test_afinacao_em_temperamento_igual)
{
	/* Âncoras: Lá 4 = 440 Hz, e as notas arredondadas ao Hz. */
	zassert_equal(440U, parser_rtttl_frequency(PITCH(4, 9)));
	zassert_equal(262U, parser_rtttl_frequency(PITCH(4, 0)));
	zassert_equal(1319U, parser_rtttl_frequency(PITCH(6, 4)));
	zassert_equal(7040U, parser_rtttl_frequency(PITCH(8, 9)));
	zassert_equal(33U, parser_rtttl_frequency(PITCH(1, 0)));

	/* Acima da oitava 8, a tabela dobra. */
	zassert_equal(8372U, parser_rtttl_frequency(PITCH(9, 0)));
	zassert_equal(28160U, parser_rtttl_frequency(PITCH(10, 9)));

	/* Fora da tabela. */
	zassert_equal(0U, parser_rtttl_frequency(PITCH(12, 0)));

	/* Cada oitava é o dobro da anterior, a menos do arredondamento. */
	for (unsigned int pitch = PITCH(1, 0); pitch < PITCH(10, 0); pitch++) {
		uint32_t low = parser_rtttl_frequency(pitch);
		uint32_t high = parser_rtttl_frequency(pitch + 12U);

		zassert_within(high, 2U * low, 1U, "semitom %u", pitch);
	}
}

ZTEST(parser_rtttl, test_toda_nota_do_formato_cabe_na_saida)
{
	/*
	 * Oitavas de 1 a 8, mais o Si sustenido 8, que é Dó 9: o parser não
	 * confere a faixa de frequência porque nenhuma nota escrita no formato
	 * cai fora dela. Este teste é a prova.
	 */
	for (unsigned int pitch = PITCH(1, 0); pitch <= PITCH(9, 0); pitch++) {
		zassert_true(tone_frequency_supported(parser_rtttl_frequency(pitch)),
			     "semitom %u: %u Hz", pitch, parser_rtttl_frequency(pitch));
	}
}

/* -------------------------------------------------------------------------- */
/* Arquivos de exemplo                                                        */
/* -------------------------------------------------------------------------- */

ZTEST(parser_rtttl, test_fur_elise_nota_a_nota)
{
	struct parser_rtttl parser;
	struct parser_csv_error error = {.line = 99U, .column = 99U};

	zassert_ok(parser_rtttl_init(&parser, furelise, sizeof(furelise), &error, 0U));
	zassert_equal(0U, error.line);
	zassert_equal(0U, error.column);

	/* 40 colcheias a 125 bpm: 8 sexagésimas-quartas de 30 ms cada. */
	zassert_equal(40U, parser.note_count);
	zassert_equal(40U * 240000U, parser.total_us);

	/* e6, d#6, e6, d#6, e6, b, d6, c6, a, p */
	const unsigned int opening[] = {
		PITCH(6, 4), PITCH(6, 3), PITCH(6, 4), PITCH(6, 3), PITCH(6, 4),
		PITCH(5, 11), PITCH(6, 2), PITCH(6, 0), PITCH(5, 9),
	};

	for (size_t i = 0; i < ARRAY_SIZE(opening); i++) {
		expect_note(&parser, i * 240000U, (i + 1U) * 240000U,
			    parser_rtttl_frequency(opening[i]));
	}

	/* A pausa depois do Lá não gera evento: o silêncio de articulação já
	 * está valendo. A nota seguinte vem na fatia certa.
	 */
	expect_note(&parser, 10U * 240000U, 11U * 240000U, parser_rtttl_frequency(PITCH(5, 0)));

	struct playback_event event;
	uint32_t previous = 0U;
	size_t events = 2U * 10U;

	while (parser_rtttl_next(&parser, &event) == 1) {
		zassert_true(event.offset_us >= previous, "evento %zu voltou no tempo", events);
		previous = event.offset_us;
		events++;
	}

	/* 35 notas tocadas, dois eventos cada, e o silêncio final; as 5 pausas
	 * vêm todas depois de nota, e não geram evento.
	 */
	zassert_equal(35U * 2U + 1U, events);
	zassert_equal(parser.total_us, previous);
}

ZTEST(parser_rtttl, test_ode_a_alegria_pontuadas_e_minimas)
{
	struct parser_rtttl parser;

	zassert_ok(parser_rtttl_init(&parser, odetojoy, sizeof(odetojoy), NULL, 0U));

	/* Por frase: 12 semínimas (16), uma pontuada (24), uma colcheia (8) e
	 * uma mínima (32) = 256 sexagésimas-quartas. Duas frases.
	 */
	zassert_equal(at(512U, 125U), parser.total_us);
	zassert_equal(15360000U, parser.total_us);

	/* Avança até o "e." da primeira frase: a 13.ª nota. */
	for (size_t i = 0; i < 12U * 2U; i++) {
		struct playback_event event;

		zassert_equal(1, parser_rtttl_next(&parser, &event));
	}

	uint32_t e_dotted = at(192U, 125U);

	expect_note(&parser, e_dotted, e_dotted + at(24U, 125U),
		    parser_rtttl_frequency(PITCH(5, 4)));
	expect_note(&parser, at(216U, 125U), at(224U, 125U), parser_rtttl_frequency(PITCH(5, 2)));
	expect_note(&parser, at(224U, 125U), at(256U, 125U), parser_rtttl_frequency(PITCH(5, 2)));
}

/* -------------------------------------------------------------------------- */
/* Grade e articulação                                                        */
/* -------------------------------------------------------------------------- */

ZTEST(parser_rtttl, test_grade_inteira_nao_acumula_erro)
{
	/*
	 * 100 colcheias a 63 bpm. Cada uma dura 476,190... ms: somar a duração
	 * arredondada ao ms (476) atrasaria a centésima em 19 ms. Pela grade,
	 * cada início sai de uma conta só, e erra menos de 1 µs.
	 */
	static char text[16 + (100U * 3U)];
	struct parser_rtttl parser;

	strcpy(text, "x:d=8,b=63:");
	for (size_t i = 0; i < 100U; i++) {
		strcat(text, (i == 0U) ? "c" : ",c");
	}

	zassert_ok(init(&parser, text));
	for (uint32_t k = 0; k < 100U; k++) {
		expect_note(&parser, at(8U * k, 63U), at(8U * (k + 1U), 63U),
			    parser_rtttl_frequency(PITCH(6, 0)));
	}

	/* A centésima começa em 47 142 857 µs, e não em 47 124 000. */
	zassert_equal(47142857U, at(8U * 99U, 63U));
	zassert_equal(at(800U, 63U), parser.total_us);
	expect_end(&parser);
}

ZTEST(parser_rtttl, test_articulacao_separa_notas_iguais)
{
	struct parser_rtttl parser;

	/* Longa: o silêncio é o teto de 20 ms, e não um décimo da semibreve. */
	zassert_ok(init(&parser, "x:b=60:1c,1c"));
	expect_event(&parser, 0U, parser_rtttl_frequency(PITCH(6, 0)));
	expect_event(&parser, at(64U, 60U) - 20000U, 0U);
	expect_event(&parser, at(64U, 60U), parser_rtttl_frequency(PITCH(6, 0)));

	/* Curta: um décimo da fatia. 2 sexagésimas-quartas a 900 bpm = 8333 µs,
	 * e o silêncio começa 833 µs antes do fim.
	 */
	zassert_ok(init(&parser, "x:b=900:32c"));
	expect_event(&parser, 0U, parser_rtttl_frequency(PITCH(6, 0)));
	expect_event(&parser, 8333U - 833U, 0U);
	expect_end(&parser);
}

ZTEST(parser_rtttl, test_pausas)
{
	struct parser_rtttl parser;

	/* No início, a pausa é silêncio em 0: o player aplica o primeiro evento
	 * na hora, e ele tem de ser do instante zero. A segunda pausa seguida
	 * não repete o silêncio.
	 */
	zassert_ok(init(&parser, "x:d=4,b=60:p,p,c,p"));
	expect_event(&parser, 0U, 0U);
	expect_note(&parser, at(32U, 60U), at(48U, 60U), parser_rtttl_frequency(PITCH(6, 0)));
	zassert_equal(at(64U, 60U), parser.total_us);
	expect_end(&parser);

	/* Só pausas: a faixa é silêncio do começo ao fim. */
	zassert_ok(init(&parser, "x::p,2p"));
	expect_event(&parser, 0U, 0U);
	expect_end(&parser);
	zassert_equal(at(48U, 63U), parser.total_us);
}

/* -------------------------------------------------------------------------- */
/* Cabeçalho e grafias                                                        */
/* -------------------------------------------------------------------------- */

ZTEST(parser_rtttl, test_padroes_do_formato)
{
	struct parser_rtttl parser;

	/* Sem padrões: semínima, oitava 6, 63 bpm. */
	zassert_ok(init(&parser, ":d=4,o=6,b=63:c"));
	zassert_equal(at(16U, 63U), parser.total_us);
	zassert_ok(init(&parser, "Sem padroes::c"));
	zassert_equal(at(16U, 63U), parser.total_us);
	expect_note(&parser, 0U, at(16U, 63U), parser_rtttl_frequency(PITCH(6, 0)));

	/* A vírgula sobrando antes do segundo `:` é tolerada. */
	zassert_ok(init(&parser, "x:o=5,:c"));
	expect_event(&parser, 0U, parser_rtttl_frequency(PITCH(5, 0)));
}

ZTEST(parser_rtttl, test_grafias_aceitas)
{
	struct parser_rtttl parser;

	/* Maiúsculas, espaços, tabulações e quebras de linha entre campos; a
	 * nota escrita de todos os jeitos que os arquivos reais usam.
	 */
	zassert_ok(init(&parser, "Nome : D = 8 , O = 4 , B = 120 :\r\n"
				 " C , \tE# , B# , h ,\n 4p. , 8c#.5 , 16D5. , 32e. ,\r\n 1f , 2G\n"));

	uint32_t t = 0U;
	const struct {
		uint32_t n64;
		unsigned int pitch;
	} notes[] = {
		{8U, PITCH(4, 0)},  /* C: padrões d=8, o=4 */
		{8U, PITCH(4, 5)},  /* E#: é Fá */
		{8U, PITCH(5, 0)},  /* B#: é o Dó da oitava de cima */
		{8U, PITCH(4, 11)}, /* h: Si alemão */
	};

	for (size_t i = 0; i < ARRAY_SIZE(notes); i++) {
		expect_note(&parser, at(t, 120U), at(t + notes[i].n64, 120U),
			    parser_rtttl_frequency(notes[i].pitch));
		t += notes[i].n64;
	}

	/* 4p.: semínima pontuada de pausa, sem evento depois do silêncio. */
	t += 24U;

	const struct {
		uint32_t n64;
		unsigned int pitch;
	} rest[] = {
		{12U, PITCH(5, 1)}, /* 8c#.5: ponto antes da oitava */
		{6U, PITCH(5, 2)},  /* 16D5.: ponto depois */
		{3U, PITCH(4, 4)},  /* 32e.: semifusa pontuada, 3/64 */
		{64U, PITCH(4, 5)}, /* 1f */
		{32U, PITCH(4, 7)}, /* 2G */
	};

	for (size_t i = 0; i < ARRAY_SIZE(rest); i++) {
		expect_note(&parser, at(t, 120U), at(t + rest[i].n64, 120U),
			    parser_rtttl_frequency(rest[i].pitch));
		t += rest[i].n64;
	}

	zassert_equal(at(t, 120U), parser.total_us);
	expect_end(&parser);
}

/* -------------------------------------------------------------------------- */
/* Erros                                                                      */
/* -------------------------------------------------------------------------- */

struct error_case {
	const char *text;
	int code;
	size_t line;
	size_t column;
};

static void expect_errors(const struct error_case *cases, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		struct parser_rtttl parser;
		struct parser_csv_error error;

		zassert_equal(cases[i].code,
			      parser_rtttl_init(&parser, (const uint8_t *)cases[i].text,
						strlen(cases[i].text), &error, 0U),
			      "caso %zu: %s", i, cases[i].text);
		zassert_equal(cases[i].line, error.line, "caso %zu: %s", i, cases[i].text);
		zassert_equal(cases[i].column, error.column, "caso %zu: %s", i, cases[i].text);
		zassert_is_null(parser.buffer, "caso %zu", i);
	}
}

ZTEST(parser_rtttl, test_erros_no_cabecalho)
{
	const struct error_case cases[] = {
		{"nome", -EINVAL, 1U, 5U},          /* sem `:` */
		{"no\nme:d=4:c", -EINVAL, 1U, 3U},  /* nome em duas linhas */
		{"n:x=4:c", -EINVAL, 1U, 3U},       /* padrão desconhecido */
		{"n:d4:c", -EINVAL, 1U, 4U},        /* falta o `=` */
		{"n:d=:c", -EINVAL, 1U, 5U},        /* falta o valor */
		{"n:d=3:c", -EINVAL, 1U, 5U},       /* duração que não existe */
		{"n:o=0:c", -ERANGE, 1U, 5U},
		{"n:o=9:c", -ERANGE, 1U, 5U},
		{"n:b=0:c", -ERANGE, 1U, 5U},
		{"n:b=901:c", -ERANGE, 1U, 5U},
		/* 4294967296 + 120: sem a saturação, daria a volta em 32 bits e
		 * cairia em 120 bpm, um andamento válido.
		 */
		{"n:b=4294967416:c", -ERANGE, 1U, 5U},
		{"n:d=4;o=5:c", -EINVAL, 1U, 6U},   /* separador errado */
		{"n:d=4", -EINVAL, 1U, 6U},         /* acaba sem o segundo `:` */
		{"n:d=4,", -EINVAL, 1U, 7U},
		{"n::", -EINVAL, 1U, 4U},           /* sem notas */
		{"n:: \r\n\t", -EINVAL, 2U, 2U},
	};

	expect_errors(cases, ARRAY_SIZE(cases));
}

ZTEST(parser_rtttl, test_erros_nas_notas)
{
	const struct error_case cases[] = {
		{"n::3c", -EINVAL, 1U, 4U},         /* duração que não existe */
		{"n::0c", -EINVAL, 1U, 4U},
		{"n::64c", -EINVAL, 1U, 4U},        /* 64 pontuada não caberia na grade */
		{"n::x", -EINVAL, 1U, 4U},          /* letra depois de h */
		{"n::#", -EINVAL, 1U, 4U},          /* antes de a */
		{"n::4", -EINVAL, 1U, 5U},          /* duração sem nota */
		{"n::p#", -EINVAL, 1U, 5U},         /* pausa sustenida */
		{"n::c0", -ERANGE, 1U, 5U},
		{"n::c9", -ERANGE, 1U, 5U},
		{"n::c.5.", -EINVAL, 1U, 7U},       /* dois pontos */
		{"n::c d", -EINVAL, 1U, 6U},        /* falta a vírgula */
		{"n::cz", -EINVAL, 1U, 5U},
		{"n::c,", -EINVAL, 1U, 6U},         /* vírgula no fim */
		{"n::c, \n", -EINVAL, 2U, 1U},
		{"n::c,\n x", -EINVAL, 2U, 2U},     /* na segunda linha */
	};

	expect_errors(cases, ARRAY_SIZE(cases));
}

ZTEST(parser_rtttl, test_faixa_cabe_em_32_bits_de_us)
{
	/* A 1 bpm, cada semibreve dura 240 s: 17 cabem em 32 bits de µs, e a
	 * 18.ª passa de 4294 s.
	 */
	static char text[16 + (18U * 3U)];
	struct parser_rtttl parser;
	struct parser_csv_error error;

	strcpy(text, "x:b=1:");
	for (size_t i = 0; i < 17U; i++) {
		strcat(text, (i == 0U) ? "1c" : ",1c");
	}
	zassert_ok(init(&parser, text));
	zassert_equal(4080000000U, parser.total_us);

	strcat(text, ",1c");
	zassert_equal(-ERANGE, parser_rtttl_init(&parser, (const uint8_t *)text, strlen(text),
						 &error, 0U));
	zassert_equal(strlen(text) - 1U, error.column, "o erro aponta a 18.a nota");
}

ZTEST(parser_rtttl, test_argumentos_invalidos)
{
	struct parser_rtttl parser = {0};
	struct parser_csv_error error = {.line = 99U, .column = 99U};
	struct playback_event event = {.offset_us = 123U};

	zassert_equal(-EINVAL, parser_rtttl_init(NULL, furelise, sizeof(furelise), &error, 0U));
	zassert_equal(0U, error.line);
	zassert_equal(-EINVAL, parser_rtttl_init(&parser, NULL, 1U, NULL, 0U));
	zassert_equal(-EINVAL, parser_rtttl_init(&parser, furelise, 0U, NULL, 0U));
	zassert_equal(-EINVAL, parser_rtttl_next(NULL, &event));
	zassert_equal(-EINVAL, parser_rtttl_next(&parser, NULL));
	zassert_equal(-EINVAL, parser_rtttl_next(&parser, &event));
	zassert_equal(-EINVAL, parser_rtttl_rewind(NULL));
	zassert_equal(-EINVAL, parser_rtttl_rewind(&parser));
	zassert_equal(123U, event.offset_us);

	/* Uma inicialização que falha, mesmo sem diagnóstico, não deixa a faixa
	 * anterior utilizável.
	 */
	zassert_ok(parser_rtttl_init(&parser, furelise, sizeof(furelise), NULL, 0U));
	zassert_equal(-EINVAL, init(&parser, "sem cabecalho"));
	zassert_equal(-EINVAL, parser_rtttl_next(&parser, &event));
}

ZTEST(parser_rtttl, test_rewind_no_meio_e_depois_do_fim)
{
	struct parser_rtttl parser;
	struct playback_event event;

	zassert_ok(parser_rtttl_init(&parser, furelise, sizeof(furelise), NULL, 0U));

	/* No meio de uma nota, com o silêncio dela ainda por entregar. */
	expect_event(&parser, 0U, parser_rtttl_frequency(PITCH(6, 4)));
	zassert_ok(parser_rtttl_rewind(&parser));
	expect_note(&parser, 0U, 240000U, parser_rtttl_frequency(PITCH(6, 4)));

	while (parser_rtttl_next(&parser, &event) == 1) {
	}
	zassert_ok(parser_rtttl_rewind(&parser));
	expect_event(&parser, 0U, parser_rtttl_frequency(PITCH(6, 4)));
}

/* -------------------------------------------------------------------------- */
/* Transposição                                                               */
/* -------------------------------------------------------------------------- */

ZTEST(parser_rtttl, test_transposicao_por_oitavas)
{
	static const char melody[] = "x:o=4:c,e,g";
	struct parser_rtttl parser;

	/* Desligada: toca o que está escrito. */
	zassert_ok(parser_rtttl_init(&parser, (const uint8_t *)melody, strlen(melody), NULL, 0U));
	zassert_equal(0U, parser.shift);
	expect_event(&parser, 0U, 262U);

	/* Já acima do limiar: nada muda. */
	zassert_ok(parser_rtttl_init(&parser, (const uint8_t *)melody, strlen(melody), NULL,
				     200U));
	zassert_equal(0U, parser.shift);

	/* Dó 4 (262 Hz) abaixo de 500: sobe uma oitava, e os intervalos ficam. */
	zassert_ok(parser_rtttl_init(&parser, (const uint8_t *)melody, strlen(melody), NULL,
				     500U));
	zassert_equal(1U, parser.shift);
	expect_note(&parser, 0U, at(16U, 63U), parser_rtttl_frequency(PITCH(5, 0)));
	expect_note(&parser, at(16U, 63U), at(32U, 63U), parser_rtttl_frequency(PITCH(5, 4)));
	expect_note(&parser, at(32U, 63U), at(48U, 63U), parser_rtttl_frequency(PITCH(5, 7)));

	/* Limiar que nem a oitava mais alta alcança: sobe até a nota mais
	 * aguda ainda caber na saída — Sol 9, 12 544 Hz; Sol 10 passaria.
	 */
	zassert_ok(parser_rtttl_init(&parser, (const uint8_t *)melody, strlen(melody), NULL,
				     TONE_FREQ_MAX_HZ));
	zassert_equal(5U, parser.shift);
	expect_event(&parser, 0U, parser_rtttl_frequency(PITCH(9, 0)));
	zassert_true(tone_frequency_supported(parser_rtttl_frequency(PITCH(9, 7))));
	zassert_false(tone_frequency_supported(parser_rtttl_frequency(PITCH(10, 7))));

	/* Só pausas: não há nota para transpor. */
	zassert_ok(parser_rtttl_init(&parser, (const uint8_t *)"x::p", 4U, NULL, 500U));
	zassert_equal(0U, parser.shift);
}

/* -------------------------------------------------------------------------- */
/* Fachada                                                                    */
/* -------------------------------------------------------------------------- */

ZTEST(parser_rtttl, test_fachada_reconhece_o_formato_pelo_conteudo)
{
	struct parser parser;
	struct playback_event event;
	struct parser_csv_error error = {.line = 99U};

	zassert_ok(parser_init(&parser, furelise, sizeof(furelise), &error));
	zassert_true(parser.rtttl);
	zassert_equal(40U * 240000U, parser_total_us(&parser));
	zassert_equal(1, parser_next(&parser, &event));
	zassert_equal(parser_rtttl_frequency(PITCH(6, 4)), event.freq_hz);
	zassert_false(parser_finished(&parser));
	while (parser_next(&parser, &event) == 1) {
	}
	zassert_true(parser_finished(&parser));
	zassert_ok(parser_rewind(&parser));
	zassert_false(parser_finished(&parser));

	zassert_ok(parser_init(&parser, escala, sizeof(escala), &error));
	zassert_false(parser.rtttl);
	zassert_equal(2000000U, parser_total_us(&parser));
	zassert_equal(1, parser_next(&parser, &event));
	zassert_equal(523U, event.freq_hz);
	while (parser_next(&parser, &event) == 1) {
	}
	zassert_true(parser_finished(&parser));
	zassert_ok(parser_rewind(&parser));
	zassert_false(parser_finished(&parser));

	/* Menor que o cabeçalho do CSV: vai ao RTTTL, que o recusa. */
	zassert_equal(-EINVAL, parser_init(&parser, (const uint8_t *)"freq", 4U, &error));
	zassert_true(parser.rtttl);
	zassert_equal(-EINVAL, parser_init(&parser, NULL, 4U, &error));
	zassert_equal(-EINVAL, parser_init(NULL, furelise, sizeof(furelise), &error));
	zassert_equal(0U, error.line);
	zassert_equal(-EINVAL, parser_next(NULL, &event));
	zassert_equal(-EINVAL, parser_rewind(NULL));
}

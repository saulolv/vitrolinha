/**
 * @file main.c
 * @brief Testes do contrato CSV: validação integral, eventos e reinício.
 *
 * Verifica arquivos reais, limites numéricos, posições dos erros e leitura
 * limitada pelo tamanho do buffer. Não depende de E/S, timer ou buzzer.
 */

#include <vitrolinha/parser_csv.h>

#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#define HEADER "frequency_hz,duration_ms"

static const uint8_t escala[] = {
#include "escala.inc"
};
static const uint8_t pausas[] = {
#include "pausas.inc"
};
static const uint8_t repetidas[] = {
#include "notas_repetidas.inc"
};
/* A faixa longa de bancada: 84,6 s, perto de dez filas de 32 eventos. */
static const uint8_t mario[] = {
#include "mario.inc"
};

/* Cabe no buffer de faixa, senão a carga a recusaria antes do parser. */
BUILD_ASSERT(sizeof(mario) <= 4096U, "mario.csv passou do buffer de faixa");

ZTEST_SUITE(parser_csv, NULL, NULL, NULL, NULL, NULL);

/** @brief Confere um evento e os metadados preservados pelo iterador. */
static void expect_event(struct parser_csv *parser, uint32_t offset, uint32_t frequency)
{
	struct playback_event event;
	uint32_t total = parser->total_us;
	size_t rows = parser->row_count;

	zassert_equal(1, parser_csv_next(parser, &event));
	zassert_equal(offset, event.offset_us);
	zassert_equal(frequency, event.freq_hz);
	zassert_equal(total, parser->total_us);
	zassert_equal(rows, parser->row_count);
}

ZTEST(parser_csv, test_escala_fixture)
{
	const uint32_t frequencies[] = {523, 587, 659, 698, 784, 880, 988, 1047};
	struct parser_csv parser;
	struct parser_csv_error error = {.line = 99U, .column = 99U};

	zassert_ok(parser_csv_init(&parser, escala, sizeof(escala), &error));
	zassert_equal(0U, error.line);
	zassert_equal(0U, error.column);
	zassert_equal(2000000U, parser.total_us);
	zassert_equal(8U, parser.row_count);
	for (size_t i = 0; i < ARRAY_SIZE(frequencies); i++) {
		expect_event(&parser, i * 250000U, frequencies[i]);
	}
	expect_event(&parser, 2000000U, 0U);

	struct playback_event sentinel = {.offset_us = 123U, .freq_hz = 456U};

	for (size_t i = 0; i < 2U; i++) {
		zassert_equal(0, parser_csv_next(&parser, &sentinel));
		zassert_equal(123U, sentinel.offset_us);
		zassert_equal(456U, sentinel.freq_hz);
	}
}

ZTEST(parser_csv, test_faixa_longa_valida_inteira)
{
	struct parser_csv parser;
	struct playback_event event;
	struct parser_csv_error error = {.line = 99U, .column = 99U};
	uint32_t previous = 0U;
	size_t events = 0U;
	int status;

	zassert_ok(parser_csv_init(&parser, mario, sizeof(mario), &error),
		   "linha %zu, coluna %zu", error.line, error.column);
	zassert_equal(309U, parser.row_count);
	zassert_equal(84582000U, parser.total_us);

	/* Toda frequência dentro do que o buzzer aceita, e os instantes só
	 * andam para a frente até o silêncio final.
	 */
	while ((status = parser_csv_next(&parser, &event)) == 1) {
		zassert_true((event.freq_hz == 0U) ||
			     ((event.freq_hz >= 20U) && (event.freq_hz <= 20000U)),
			     "evento %zu com %u Hz", events, event.freq_hz);
		zassert_true(event.offset_us >= previous, "evento %zu voltou no tempo", events);
		previous = event.offset_us;
		events++;
	}

	zassert_equal(0, status);
	zassert_equal(309U + 1U, events, "uma por linha, mais o silencio final");
	zassert_equal(84582000U, previous);
}

ZTEST(parser_csv, test_pausas_fixture_e_silencio_final)
{
	struct parser_csv parser;

	zassert_ok(parser_csv_init(&parser, pausas, sizeof(pausas), NULL));
	zassert_equal(600000U, parser.total_us);
	expect_event(&parser, 0U, 1000U);
	expect_event(&parser, 200000U, 0U);
	expect_event(&parser, 300000U, 1500U);
	expect_event(&parser, 500000U, 0U);
	expect_event(&parser, 600000U, 0U);
}

ZTEST(parser_csv, test_repetidas_fixture_sem_articulacao)
{
	struct parser_csv parser;

	zassert_ok(parser_csv_init(&parser, repetidas, sizeof(repetidas), NULL));
	zassert_equal(700000U, parser.total_us);
	expect_event(&parser, 0U, 1000U);
	expect_event(&parser, 200000U, 1000U);
	expect_event(&parser, 400000U, 0U);
	expect_event(&parser, 500000U, 1000U);
	expect_event(&parser, 700000U, 0U);
}

ZTEST(parser_csv, test_quebras_de_linha_e_fim_sem_newline)
{
	const char *cases[] = {
		HEADER "\n1000,200\n0,100\n1500,200\n",
		HEADER "\r\n1000,200\r\n0,100\r\n1500,200\r\n",
		HEADER "\r\n1000,200\n0,100\r\n1500,200",
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		struct parser_csv parser;

		zassert_ok(parser_csv_init(&parser, (const uint8_t *)cases[i],
					  strlen(cases[i]), NULL));
		zassert_equal(3U, parser.row_count);
		zassert_equal(500000U, parser.total_us);
		expect_event(&parser, 0U, 1000U);
		expect_event(&parser, 200000U, 0U);
		expect_event(&parser, 300000U, 1500U);
		expect_event(&parser, 500000U, 0U);
		zassert_ok(parser_csv_rewind(&parser));
		expect_event(&parser, 0U, 1000U);
	}
}

ZTEST(parser_csv, test_tamanho_exato_sem_nul)
{
	/* Não há espaço para NUL: a última leitura deve parar no byte final. */
	static const uint8_t csv[sizeof(HEADER "\n1,2") - 1U] = HEADER "\n1,2";
	static const uint8_t suffix[] = HEADER "\n1,2IGNORAR";
	struct parser_csv parser;

	zassert_ok(parser_csv_init(&parser, csv, sizeof(csv), NULL));
	expect_event(&parser, 0U, 1U);
	expect_event(&parser, 2000U, 0U);
	zassert_ok(parser_csv_init(&parser, suffix, sizeof(csv), NULL));
	expect_event(&parser, 0U, 1U);
	expect_event(&parser, 2000U, 0U);
}

ZTEST(parser_csv, test_rewind_parcial_e_apos_fim)
{
	struct parser_csv parser;
	struct playback_event event;

	zassert_ok(parser_csv_init(&parser, repetidas, sizeof(repetidas), NULL));
	expect_event(&parser, 0U, 1000U);
	expect_event(&parser, 200000U, 1000U);
	zassert_ok(parser_csv_rewind(&parser));
	expect_event(&parser, 0U, 1000U);
	while (parser_csv_next(&parser, &event) == 1) {
	}
	zassert_ok(parser_csv_rewind(&parser));
	expect_event(&parser, 0U, 1000U);
	zassert_equal(4U, parser.row_count);
	zassert_equal(700000U, parser.total_us);
}

ZTEST(parser_csv, test_argumentos_invalidos)
{
	struct parser_csv parser = {0};
	struct parser_csv_error error = {.line = 99U, .column = 99U};
	struct playback_event event = {.offset_us = 123U, .freq_hz = 456U};

	zassert_equal(-EINVAL, parser_csv_init(NULL, escala, sizeof(escala), &error));
	zassert_equal(0U, error.line);
	zassert_equal(0U, error.column);
	zassert_equal(-EINVAL, parser_csv_init(&parser, NULL, 1U, NULL));
	zassert_equal(-EINVAL, parser_csv_init(&parser, escala, 0U, NULL));
	zassert_equal(-EINVAL, parser_csv_next(NULL, &event));
	zassert_equal(-EINVAL, parser_csv_next(&parser, NULL));
	zassert_equal(-EINVAL, parser_csv_next(&parser, &event));
	zassert_equal(-EINVAL, parser_csv_rewind(NULL));
	zassert_equal(-EINVAL, parser_csv_rewind(&parser));
	zassert_equal(123U, event.offset_us);
	zassert_equal(456U, event.freq_hz);
}

ZTEST(parser_csv, test_cabecalho_invalido_ou_sem_notas)
{
	const struct {
		const char *csv;
		size_t line;
		size_t column;
	} cases[] = {
		{"\n1,2", 1U, 1U},
		{"frequency_hz,duration_m\n1,2", 1U, 24U},
		{"Frequency_hz,duration_ms\n1,2", 1U, 1U},
		{HEADER ",extra\n1,2", 1U, 25U},
		{HEADER, 2U, 1U},
		{HEADER "\n", 2U, 1U},
		{HEADER "\r", 1U, 25U},
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		struct parser_csv parser;
		struct parser_csv_error error;

		zassert_equal(-EINVAL, parser_csv_init(&parser, (const uint8_t *)cases[i].csv,
						     strlen(cases[i].csv), &error), "caso %zu", i);
		zassert_equal(cases[i].line, error.line, "caso %zu", i);
		zassert_equal(cases[i].column, error.column, "caso %zu", i);
	}
}

ZTEST(parser_csv, test_linhas_invalidas_e_localizacao)
{
	const struct {
		const char *csv;
		size_t column;
	} cases[] = {
		{HEADER "\n\n", 1U},
		{HEADER "\n\r\n", 1U},
		{HEADER "\n,1", 1U},
		{HEADER "\n1", 2U},
		{HEADER "\n1;2", 2U},
		{HEADER "\n1,", 3U},
		{HEADER "\n1,0", 3U},
		{HEADER "\n1,2,3", 4U},
		{HEADER "\n-1,2", 1U},
		{HEADER "\n+1,2", 1U},
		{HEADER "\n1,-2", 3U},
		{HEADER "\n 1,2", 1U},
		{HEADER "\n1, 2", 3U},
		{HEADER "\n1,2 ", 4U},
		{HEADER "\n1.5,2", 2U},
		{HEADER "\n1,2.5", 4U},
		{HEADER "\n\"1\",2", 1U},
		{HEADER "\nA,2", 1U},
		{HEADER "\n1,A", 3U},
		{HEADER "\n1,2\r", 4U},
		{HEADER "\n1\r,2\n", 2U},
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		struct parser_csv parser;
		struct parser_csv_error error;

		zassert_equal(-EINVAL, parser_csv_init(&parser, (const uint8_t *)cases[i].csv,
						     strlen(cases[i].csv), &error), "caso %zu", i);
		zassert_equal(2U, error.line, "caso %zu", i);
		zassert_equal(cases[i].column, error.column, "caso %zu", i);
	}
}

ZTEST(parser_csv, test_limites_e_overflow)
{
	static const uint8_t maximum[] = HEADER "\n4294967295,4294966\n0000,0001";
	const struct {
		const char *csv;
		size_t line;
		size_t column;
	} cases[] = {
		{HEADER "\n4294967296,1", 2U, 10U},
		{HEADER "\n999999999999999999999999999,1", 2U, 10U},
		{HEADER "\n1,4294967296", 2U, 12U},
		{HEADER "\n1,4294967295", 2U, 3U},
		{HEADER "\n1,4294968", 2U, 3U},
		{HEADER "\n1,4294967\n1,1", 3U, 3U},
	};
	struct parser_csv parser;

	zassert_ok(parser_csv_init(&parser, maximum, sizeof(maximum) - 1U, NULL));
	zassert_equal(4294967000U, parser.total_us);
	expect_event(&parser, 0U, UINT32_MAX);
	expect_event(&parser, 4294966000U, 0U);
	expect_event(&parser, 4294967000U, 0U);
	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		struct parser_csv_error error;

		zassert_equal(-ERANGE, parser_csv_init(&parser, (const uint8_t *)cases[i].csv,
						     strlen(cases[i].csv), &error), "caso %zu", i);
		zassert_equal(cases[i].line, error.line, "caso %zu", i);
		zassert_equal(cases[i].column, error.column, "caso %zu", i);
	}
}

ZTEST(parser_csv, test_validacao_integral_e_reinicializacao)
{
	static const uint8_t bad[] = HEADER "\n1000,200\n1500,0";
	struct parser_csv parser;
	struct parser_csv_error error;
	struct playback_event event;

	zassert_ok(parser_csv_init(&parser, escala, sizeof(escala), NULL));
	expect_event(&parser, 0U, 523U);
	zassert_equal(-EINVAL, parser_csv_init(&parser, bad, sizeof(bad) - 1U, &error));
	zassert_equal(3U, error.line);
	zassert_equal(6U, error.column);
	zassert_is_null(parser.buffer);
	zassert_equal(-EINVAL, parser_csv_next(&parser, &event));
	zassert_equal(-EINVAL, parser_csv_rewind(&parser));
	/* O diagnóstico é opcional também em caso de falha. */
	zassert_equal(-EINVAL, parser_csv_init(&parser, bad, sizeof(bad) - 1U, NULL));
	zassert_ok(parser_csv_init(&parser, pausas, sizeof(pausas), &error));
	zassert_equal(0U, error.line);
	expect_event(&parser, 0U, 1000U);
}

ZTEST(parser_csv, test_nul_interno_e_byte_nao_ascii)
{
	static const uint8_t nul[] = HEADER "\n1,2\0\n1,3";
	static const uint8_t non_ascii[] = HEADER "\n\xff,2";
	struct parser_csv parser;
	struct parser_csv_error error;

	zassert_equal(-EINVAL, parser_csv_init(&parser, nul, sizeof(nul) - 1U, &error));
	zassert_equal(2U, error.line);
	zassert_equal(4U, error.column);
	zassert_equal(-EINVAL, parser_csv_init(&parser, non_ascii,
					     sizeof(non_ascii) - 1U, &error));
	zassert_equal(1U, error.column);
}

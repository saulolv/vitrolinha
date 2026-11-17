/**
 * @file main.c
 * @brief Testes de @ref track.h.
 *
 * O que se verifica aqui é uma invariante, não um valor: **nada que saia
 * destas funções fica sem terminador**, independentemente do que o chamador
 * passe. O nome da faixa é o único texto da tela, e uma string sem
 * terminador viraria lixo no display ou leitura fora dos limites.
 */

#include <vitrolinha/track.h>

#include <string.h>

#include <zephyr/ztest.h>

ZTEST_SUITE(track, NULL, NULL, NULL, NULL, NULL);

/* -------------------------------------------------------------------------
 * track_name_copy: argumentos degenerados
 * ------------------------------------------------------------------------- */

ZTEST(track, test_name_copy_dst_nulo)
{
	zassert_equal(0U, track_name_copy(NULL, 16U, "abc", 3U),
		      "destino nulo deve devolver 0 sem escrever");
}

ZTEST(track, test_name_copy_dst_sem_espaco)
{
	char dst[4] = {'x', 'x', 'x', 'x'};

	zassert_equal(0U, track_name_copy(dst, 0U, "abc", 3U));
	zassert_equal('x', dst[0], "tamanho zero nao pode escrever nem o terminador");
}

ZTEST(track, test_name_copy_src_nulo)
{
	char dst[8];

	zassert_equal(0U, track_name_copy(dst, sizeof(dst), NULL, 5U));
	zassert_str_equal("", dst, "origem nula deve produzir string vazia");
}

ZTEST(track, test_name_copy_src_vazia)
{
	char dst[8];

	zassert_equal(0U, track_name_copy(dst, sizeof(dst), "abc", 0U));
	zassert_str_equal("", dst);
}

/* -------------------------------------------------------------------------
 * track_name_copy: caminhos normais
 * ------------------------------------------------------------------------- */

ZTEST(track, test_name_copy_cabe_com_folga)
{
	char dst[8];

	zassert_equal(3U, track_name_copy(dst, sizeof(dst), "abc", 3U));
	zassert_str_equal("abc", dst);
}

ZTEST(track, test_name_copy_cabe_exato)
{
	char dst[4];

	/* Três caracteres num destino de quatro bytes: o último é o
	 * terminador. É a fronteira em que um erro de mais-ou-menos-um
	 * apareceria.
	 */
	zassert_equal(3U, track_name_copy(dst, sizeof(dst), "abc", 3U));
	zassert_str_equal("abc", dst);
}

ZTEST(track, test_name_copy_trunca)
{
	char dst[4];

	zassert_equal(3U, track_name_copy(dst, sizeof(dst), "abcdef", 6U),
		      "deve escrever dst_size-1 caracteres");
	zassert_str_equal("abc", dst);
}

ZTEST(track, test_name_copy_origem_sem_terminador)
{
	/* O caso real: o nome no cabeçalho RTTTL termina em ':', não em NUL. */
	static const char header[] = "Fur Elise:d=8,o=5,b=125:e6,d#6";
	char dst[TRACK_NAME_MAX];
	const char *colon = strchr(header, ':');
	size_t name_len = (size_t)(colon - header);

	zassert_equal(name_len, track_name_copy(dst, sizeof(dst), header, name_len));
	zassert_str_equal("Fur Elise", dst);
}

/* -------------------------------------------------------------------------
 * track_name_copy: truncagem em UTF-8
 * ------------------------------------------------------------------------- */

ZTEST(track, test_name_copy_nao_parte_acento)
{
	/* "Ação": o corte em 2 bytes cairia entre o 0xC3 e o 0xA7 do "ç".
	 * Um 0xC3 solto no fim do nome a tela desenharia como lixo.
	 */
	static const char acao[] = "A\xC3\xA7\xC3\xA3o";
	char dst[3];

	zassert_equal(1U, track_name_copy(dst, sizeof(dst), acao, sizeof(acao) - 1U));
	zassert_str_equal("A", dst);
}

ZTEST(track, test_name_copy_corte_na_fronteira_fica_onde_esta)
{
	/* O corte já cai antes do líder do "é": nada a recuar. */
	static const char cafe[] = "a\xC3\xA9";
	char dst[2];

	zassert_equal(1U, track_name_copy(dst, sizeof(dst), cafe, sizeof(cafe) - 1U));
	zassert_str_equal("a", dst);
}

ZTEST(track, test_name_copy_nao_parte_sequencia_de_quatro_bytes)
{
	/* A nota musical U+1F3B5 ocupa quatro bytes: cortar depois do
	 * segundo obriga a recuar dois.
	 */
	static const char nota[] = "x\xF0\x9F\x8E\xB5";
	char dst[4];

	zassert_equal(1U, track_name_copy(dst, sizeof(dst), nota, sizeof(nota) - 1U));
	zassert_str_equal("x", dst);
}

ZTEST(track, test_name_copy_recuo_tem_limite)
{
	/* Continuações sem líder não são UTF-8, são lixo. O recuo para em
	 * três bytes, o máximo de um caractere de verdade, em vez de apagar o
	 * nome inteiro procurando um começo que não existe.
	 */
	static const char lixo[] = "\x80\x80\x80\x80\x80";
	char dst[5];

	zassert_equal(1U, track_name_copy(dst, sizeof(dst), lixo, sizeof(lixo) - 1U));
}

ZTEST(track, test_name_copy_recuo_para_no_inicio)
{
	static const char lixo[] = "\x80\x80";
	char dst[2];

	zassert_equal(0U, track_name_copy(dst, sizeof(dst), lixo, sizeof(lixo) - 1U));
	zassert_str_equal("", dst);
}

/* -------------------------------------------------------------------------
 * track_meta_init
 * ------------------------------------------------------------------------- */

ZTEST(track, test_meta_init_nulo)
{
	/* Não pode explodir: a varredura chama isto num laço e um erro de
	 * índice não deve derrubar o firmware.
	 */
	track_meta_init(NULL, 0U, "abc", 3U, true);
}

ZTEST(track, test_meta_init_preenche)
{
	struct track_meta meta;

	track_meta_init(&meta, 7U, "Ode to Joy", 10U, true);

	zassert_str_equal("Ode to Joy", meta.name);
	zassert_equal(7U, meta.index);
	zassert_true(meta.valid);
}

ZTEST(track, test_meta_init_marca_invalida)
{
	struct track_meta meta;

	track_meta_init(&meta, 0U, "Quebrada", 8U, false);

	zassert_false(meta.valid, "arquivo rejeitado pelo interpretador");
}

ZTEST(track, test_meta_init_trunca_nome_longo)
{
	struct track_meta meta;
	static const char longo[] = "Um nome bem maior que o campo de destino";

	track_meta_init(&meta, 0U, longo, sizeof(longo) - 1U, true);

	zassert_equal(TRACK_NAME_MAX - 1U, strlen(meta.name));
	zassert_equal('\0', meta.name[TRACK_NAME_MAX - 1U],
		      "o ultimo byte tem de continuar sendo o terminador");
}

ZTEST(track, test_meta_init_nao_parte_acento_no_fim_do_campo)
{
	struct track_meta meta;
	/* 22 bytes ASCII e um "é" de dois: o campo comporta 23, e o corte
	 * cairia no meio do acento.
	 */
	static const char nome[] = "Cancao de ninar do Ze \xC3\xA9";

	track_meta_init(&meta, 0U, nome, sizeof(nome) - 1U, true);

	zassert_equal(22U, strlen(meta.name));
	zassert_str_equal("Cancao de ninar do Ze ", meta.name);
}

ZTEST(track, test_meta_init_zera_residuo)
{
	struct track_meta meta;

	/* Reaproveitar uma entrada de nome longo com um nome curto não pode
	 * deixar bytes da anterior depois do terminador: duas faixas de mesmo
	 * nome precisam ser iguais byte a byte.
	 */
	track_meta_init(&meta, 0U, "Nome comprido aqui", 18U, true);
	track_meta_init(&meta, 0U, "Ab", 2U, true);

	for (size_t i = 3U; i < TRACK_NAME_MAX; i++) {
		zassert_equal('\0', meta.name[i], "byte residual na posicao %zu", i);
	}
}

ZTEST(track, test_meta_init_iguais_sao_identicos)
{
	struct track_meta a;
	struct track_meta b;

	track_meta_init(&a, 3U, "Fur Elise", 9U, true);
	track_meta_init(&b, 3U, "Fur Elise", 9U, true);

	zassert_mem_equal(&a, &b, sizeof(a),
			  "entradas iguais devem ser identicas byte a byte");
}

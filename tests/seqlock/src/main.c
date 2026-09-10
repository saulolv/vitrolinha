/**
 * @file main.c
 * @brief Testes de @ref seqlock.h.
 *
 * O @ref seqlock existe separado do @ref snapshot.h justamente para poder ser
 * testado assim: as duas situações que interessam — leitura que começa no
 * meio de uma escrita, e escrita que termina durante a leitura — aqui são
 * provocadas de forma determinística, sem depender de duas threads se
 * cruzarem na hora certa.
 */

#include <vitrolinha/seqlock.h>

#include <zephyr/ztest.h>

ZTEST_SUITE(seqlock, NULL, NULL, NULL, NULL, NULL);

ZTEST(seqlock, test_leitura_sem_escritor_nao_repete)
{
	struct seqlock sl = SEQLOCK_INITIALIZER;
	uint32_t begin = seqlock_read_begin(&sl);

	zassert_false(seqlock_read_retry(&sl, begin),
		      "sem escrita concorrente, a copia vale");
}

ZTEST(seqlock, test_escrita_completa_invalida_leitura_em_curso)
{
	struct seqlock sl = SEQLOCK_INITIALIZER;
	uint32_t begin;
	unsigned int key;

	/* Leitor amostra o contador... */
	begin = seqlock_read_begin(&sl);

	/* ...e um ciclo inteiro de escrita acontece antes de ele conferir. */
	key = seqlock_write_lock(&sl);
	seqlock_write_unlock(&sl, key);

	zassert_true(seqlock_read_retry(&sl, begin),
		     "escrita concluida durante a copia tem de forcar repeticao");
}

ZTEST(seqlock, test_leitura_iniciada_no_meio_da_escrita_repete)
{
	struct seqlock sl = SEQLOCK_INITIALIZER;
	unsigned int key;
	uint32_t begin;

	key = seqlock_write_lock(&sl);

	/* Amostra tirada com escrita em curso: o contador está ímpar. */
	begin = seqlock_read_begin(&sl);
	zassert_equal(1U, begin & 1U, "escrita em curso deixa o contador impar");
	zassert_true(seqlock_read_retry(&sl, begin),
		     "copia iniciada no meio da escrita nao vale");

	seqlock_write_unlock(&sl, key);
}

ZTEST(seqlock, test_contador_estavel_e_par_fora_da_escrita)
{
	struct seqlock sl = SEQLOCK_INITIALIZER;
	unsigned int key;

	zassert_equal(0U, seqlock_read_begin(&sl) & 1U, "estado inicial e estavel");

	key = seqlock_write_lock(&sl);
	seqlock_write_unlock(&sl, key);

	zassert_equal(0U, seqlock_read_begin(&sl) & 1U,
		      "depois de fechar a escrita, estavel de novo");
}

ZTEST(seqlock, test_escritas_sucessivas_avancam_o_contador)
{
	struct seqlock sl = SEQLOCK_INITIALIZER;
	uint32_t antes = seqlock_read_begin(&sl);
	uint32_t depois;
	unsigned int key;

	for (int i = 0; i < 3; i++) {
		key = seqlock_write_lock(&sl);
		seqlock_write_unlock(&sl, key);
	}

	depois = seqlock_read_begin(&sl);

	/* Duas incrementações por escrita. O valor exato importa pouco; o que
	 * não pode é voltar ao mesmo número, senão um leitor lento aceitaria
	 * uma cópia rasgada.
	 */
	zassert_not_equal(antes, depois);
	zassert_equal(6U, depois - antes);
}

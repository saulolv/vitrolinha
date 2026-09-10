/**
 * @file main.c
 * @brief Testes de @ref snapshot.h.
 *
 * Duas coisas se verificam aqui:
 *
 * 1. A conversão de tempo, que é onde mora a dupla leitura de
 *    @ref player_snapshot::base_us. É pura, então é testada exaustivamente.
 * 2. A integridade da publicação sob concorrência: um escritor alternando
 *    entre dois instantâneos totalmente distintos, e um leitor que nunca
 *    pode enxergar uma mistura dos dois.
 */

#include <vitrolinha/snapshot.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/** @brief Devolve o instantâneo compartilhado ao repouso entre os casos. */
static void snapshot_before(void *fixture)
{
	const struct player_snapshot idle = SNAPSHOT_IDLE_INITIALIZER;

	ARG_UNUSED(fixture);
	snapshot_publish(&idle);
}

ZTEST_SUITE(snapshot, NULL, NULL, snapshot_before, NULL, NULL);

/* -------------------------------------------------------------------------
 * Publicação e leitura
 * ------------------------------------------------------------------------- */

ZTEST(snapshot, test_estado_inicial_e_repouso)
{
	struct player_snapshot got;

	snapshot_read(&got);

	zassert_equal((uint32_t)PLAYER_STOPPED, got.state);
	zassert_equal(TRACK_NONE, got.track,
		      "faixa de repouso e TRACK_NONE, nao zero");
	zassert_equal(0U, got.total_us);
}

ZTEST(snapshot, test_publica_e_le_de_volta)
{
	const struct player_snapshot sent = {
		.state = PLAYER_PLAYING,
		.base_us = 1234567890123ULL,
		.total_us = 42U,
		.track = 7,
	};
	struct player_snapshot got;

	snapshot_publish(&sent);
	snapshot_read(&got);

	zassert_mem_equal(&sent, &got, sizeof(sent));
}

ZTEST(snapshot, test_publicar_nulo_nao_altera)
{
	const struct player_snapshot sent = {
		.state = PLAYER_PAUSED,
		.base_us = 99U,
		.total_us = 100U,
		.track = 1,
	};
	struct player_snapshot got;

	snapshot_publish(&sent);
	snapshot_publish(NULL);
	snapshot_read(&got);

	zassert_mem_equal(&sent, &got, sizeof(sent),
			  "publicacao nula tem de ser ignorada, nao zerar");
}

ZTEST(snapshot, test_ler_para_nulo_nao_explode)
{
	snapshot_read(NULL);
}

/* -------------------------------------------------------------------------
 * Tempo decorrido
 * ------------------------------------------------------------------------- */

ZTEST(snapshot, test_decorrido_de_instantaneo_nulo)
{
	zassert_equal(0U, snapshot_elapsed_us(NULL, 1000U));
}

ZTEST(snapshot, test_decorrido_tocando_e_diferenca)
{
	const struct player_snapshot snap = {
		.state = PLAYER_PLAYING,
		.base_us = 1000U,
		.total_us = 10000U,
		.track = 0,
	};

	zassert_equal(500U, snapshot_elapsed_us(&snap, 1500U));
}

ZTEST(snapshot, test_decorrido_tocando_satura_em_zero)
{
	const struct player_snapshot snap = {
		.state = PLAYER_PLAYING,
		.base_us = 2000U,
		.total_us = 10000U,
		.track = 0,
	};

	/* A interface pode ler um instantâneo cuja base está no futuro
	 * imediato, entre o agendamento de uma nota e o instante dela.
	 */
	zassert_equal(0U, snapshot_elapsed_us(&snap, 1000U),
		      "base no futuro nao pode virar decorrido negativo");
}

ZTEST(snapshot, test_decorrido_tocando_limitado_ao_total)
{
	const struct player_snapshot snap = {
		.state = PLAYER_PLAYING,
		.base_us = 0U,
		.total_us = 5000U,
		.track = 0,
	};

	zassert_equal(5000U, snapshot_elapsed_us(&snap, 999999U),
		      "a barra nao pode passar do fim da faixa");
}

ZTEST(snapshot, test_decorrido_pausado_fica_congelado)
{
	const struct player_snapshot snap = {
		.state = PLAYER_PAUSED,
		.base_us = 3000U,
		.total_us = 10000U,
		.track = 0,
	};

	/* Em pausa, base_us guarda o próprio decorrido: o relógio da tela
	 * para junto com a música, por mais que o tempo passe.
	 */
	zassert_equal(3000U, snapshot_elapsed_us(&snap, 4000U));
	zassert_equal(3000U, snapshot_elapsed_us(&snap, 900000U));
}

ZTEST(snapshot, test_decorrido_parado_no_inicio)
{
	const struct player_snapshot snap = {
		.state = PLAYER_STOPPED,
		.base_us = 0U,
		.total_us = 10000U,
		.track = TRACK_NONE,
	};

	zassert_equal(0U, snapshot_elapsed_us(&snap, 555U));
}

ZTEST(snapshot, test_decorrido_no_fim_da_faixa_e_cem_por_cento)
{
	const struct player_snapshot snap = {
		.state = PLAYER_STOPPED,
		.base_us = 10000U,
		.total_us = 10000U,
		.track = 0,
	};

	/* Ao fim da faixa o player para e permanece na tela de reprodução,
	 * com o progresso cheio.
	 */
	zassert_equal(10000U, snapshot_elapsed_us(&snap, 123456U));
	zassert_equal(1000U, snapshot_progress_permille(&snap, 123456U));
}

ZTEST(snapshot, test_decorrido_congelado_limitado_ao_total)
{
	const struct player_snapshot snap = {
		.state = PLAYER_PAUSED,
		.base_us = 99999U,
		.total_us = 1000U,
		.track = 0,
	};

	zassert_equal(1000U, snapshot_elapsed_us(&snap, 0U));
}

/* -------------------------------------------------------------------------
 * Progresso
 * ------------------------------------------------------------------------- */

ZTEST(snapshot, test_progresso_de_instantaneo_nulo)
{
	zassert_equal(0U, snapshot_progress_permille(NULL, 1000U));
}

ZTEST(snapshot, test_progresso_sem_duracao_conhecida)
{
	const struct player_snapshot snap = {
		.state = PLAYER_PLAYING,
		.base_us = 0U,
		.total_us = 0U,
		.track = 0,
	};

	zassert_equal(0U, snapshot_progress_permille(&snap, 5000U),
		      "duracao zero nao pode dividir por zero");
}

ZTEST(snapshot, test_progresso_na_metade)
{
	const struct player_snapshot snap = {
		.state = PLAYER_PLAYING,
		.base_us = 0U,
		.total_us = 10000U,
		.track = 0,
	};

	zassert_equal(500U, snapshot_progress_permille(&snap, 5000U));
}

ZTEST(snapshot, test_progresso_de_faixa_longa_nao_estoura)
{
	/* 10 minutos: o produto por 1000 passa de 2^32 e exige 64 bits. */
	const struct player_snapshot snap = {
		.state = PLAYER_PLAYING,
		.base_us = 0U,
		.total_us = 600U * USEC_PER_SEC,
		.track = 0,
	};

	zassert_equal(500U, snapshot_progress_permille(&snap, 300U * USEC_PER_SEC));
	zassert_equal(1000U, snapshot_progress_permille(&snap, 600U * USEC_PER_SEC));
}

/* -------------------------------------------------------------------------
 * Integridade sob concorrência
 * ------------------------------------------------------------------------- */

/**
 * @brief Dois instantâneos sem nenhum campo em comum.
 *
 * Todo campo carrega a marca da versão a que pertence, de modo que qualquer
 * mistura entre as duas apareça em pelo menos um deles.
 */
static const struct player_snapshot alternativa[2] = {
	{
		.state = PLAYER_PLAYING,
		.base_us = 0x1111111111111111ULL,
		.total_us = 0x11111111U,
		.track = 1,
	},
	{
		.state = PLAYER_PAUSED,
		.base_us = 0x2222222222222222ULL,
		.total_us = 0x22222222U,
		.track = 2,
	},
};

/** @brief Quantas publicações o temporizador já fez. */
static volatile uint32_t publish_count;

/**
 * @brief Publica de dentro de um callback de temporizador.
 *
 * Em produção quem publica a cada nota é o callback do `k_timer`, em
 * contexto de interrupção. Reproduzir isso aqui é o que prova que
 * @ref snapshot_publish não chama nada proibido em interrupção — com
 * `CONFIG_ASSERT` ligado, uma chamada bloqueante ou uma alocação abortariam
 * o teste em vez de passar despercebidas até a placa.
 */
static void publisher_expiry(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	snapshot_publish(&alternativa[publish_count & 1U]);
	publish_count++;
}

static K_TIMER_DEFINE(publisher_timer, publisher_expiry, NULL);

/** @brief Quantas leituras o caso faz, uma por tique. */
#define CONTENTION_READS 200U

/*
 * O leitor dorme um tique entre as leituras em vez de girar em espera ativa.
 * Não é economia: em native_sim o relógio simulado só avança quando a CPU
 * fica ociosa, então um laço de espera ativa congelaria o tempo, o
 * temporizador nunca expiraria e o caso rodaria para sempre.
 *
 * A consequência é que esta plataforma não consegue interromper o leitor no
 * meio da cópia — as interrupções só acontecem nos pontos de ociosidade. O
 * caminho de repetição do @ref seqlock é coberto de forma determinística em
 * tests/seqlock; o que se verifica aqui é que publicar de interrupção
 * funciona e que leitura e publicação alternadas nunca produzem uma versão
 * misturada.
 */
ZTEST(snapshot, test_publicacao_em_interrupcao_nao_mistura_versoes)
{
	publish_count = 0U;

	/* O registro precisa começar valendo uma das duas versões: senão a
	 * primeira leitura pegaria o repouso publicado no preparo e acusaria
	 * uma mistura que não houve.
	 */
	snapshot_publish(&alternativa[0]);

	k_timer_start(&publisher_timer, K_TICKS(1), K_TICKS(1));

	for (uint32_t i = 0U; i < CONTENTION_READS; i++) {
		struct player_snapshot got;
		const struct player_snapshot *expected;

		k_sleep(K_TICKS(1));
		snapshot_read(&got);

		/* O estado é a âncora: escolhida a versão por ele, todos os
		 * outros campos têm de ser os dessa mesma versão.
		 */
		expected = (got.state == (uint32_t)PLAYER_PLAYING) ? &alternativa[0]
								   : &alternativa[1];

		zassert_equal(expected->base_us, got.base_us,
			      "leitura %u: base_us de outra versao", i);
		zassert_equal(expected->total_us, got.total_us,
			      "leitura %u: total_us de outra versao", i);
		zassert_equal(expected->track, got.track,
			      "leitura %u: faixa de outra versao", i);
	}

	k_timer_stop(&publisher_timer);

	zassert_true(publish_count > 0U,
		     "o temporizador nao chegou a publicar: o caso nao verificou nada");
}

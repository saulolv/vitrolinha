/**
 * @file main.c
 * @brief Testes de @ref library.h: a matriz de falhas do cartão.
 *
 * O critério da issue #12 é que as três falhas — sem cartão no boot, cartão
 * removido tocando, arquivo inválido — sejam tratadas como três coisas, e não
 * como uma. Cada grupo de casos abaixo é uma linha daquela tabela.
 *
 * A origem das faixas é a implementação de mentira do storage, porque é
 * nela que "puxar o cartão" é uma linha de código. O módulo sob teste não
 * sabe disso: fala com o @ref storage.h e mais nada.
 */

#include <vitrolinha/library.h>
#include <vitrolinha/library_test.h>
#include <vitrolinha/storage.h>
#include <vitrolinha/storage_fake.h>
#include <vitrolinha/track.h>

#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

/** @brief Destino das cargas, do tamanho que o contrato exige. */
static uint8_t buf[STORAGE_FILE_MAX];

/** @brief Destino das cópias da lista. */
static struct track_meta tracks[LIBRARY_MAX];

/**
 * @brief Todo caso começa no boot: cartão saudável no soquete, mas ainda
 *        não varrido, e nenhuma história de cartões anteriores.
 */
static void library_before(void *fixture)
{
	ARG_UNUSED(fixture);

	storage_fake_reset();
	library_reset();
	(void)memset(buf, 0, sizeof(buf));
	(void)memset(tracks, 0, sizeof(tracks));
}

ZTEST_SUITE(library, NULL, NULL, library_before, NULL, NULL);

/** @brief Varre com o cartão saudável e confere que a lista subiu. */
static void insert_good_card(void)
{
	storage_fake_set_present(true);
	storage_fake_set_failing(false);

	zassert_equal((int)STORAGE_FAKE_TRACK_COUNT, library_refresh());
	zassert_equal(LIBRARY_READY, library_state());
}

/** @brief Tira o cartão e deixa o monitor reagir. */
static void pull_card(void)
{
	storage_fake_set_present(false);

	zassert_equal(-ENODEV, library_refresh());
}

/** @brief Conferências comuns a todo estado sem lista. */
static void assert_no_library(void)
{
	zassert_equal(0, library_copy(tracks, ARRAY_SIZE(tracks), NULL));
	zassert_equal(TRACK_NONE, library_neighbor(0, true), "seguinte deveria estar desligado");
	zassert_equal(TRACK_NONE, library_neighbor(0, false), "anterior deveria estar desligado");
	zassert_equal(TRACK_NONE, library_neighbor(TRACK_NONE, true));
}

/* -------------------------------------------------------------------------
 * Sem cartão no boot
 * ------------------------------------------------------------------------- */

ZTEST(library, test_boot_comeca_sem_cartao)
{
	/* Antes de qualquer varredura, o conservador: nada a tocar. */
	zassert_equal(LIBRARY_NO_CARD, library_state());
	assert_no_library();
}

ZTEST(library, test_boot_com_soquete_vazio_e_sem_cartao)
{
	pull_card();

	zassert_equal(LIBRARY_NO_CARD, library_state(),
		      "sem nenhum cartao lido antes, ausencia nao e remocao");
	assert_no_library();
}

ZTEST(library, test_insercao_depois_do_boot_monta_a_lista)
{
	pull_card();
	insert_good_card();

	zassert_equal((int)STORAGE_FAKE_TRACK_COUNT,
		      library_copy(tracks, ARRAY_SIZE(tracks), NULL));
	zassert_str_equal("Fur Elise", tracks[0].name);
	zassert_str_equal("Ode to Joy", tracks[1].name);
	zassert_true(tracks[0].valid && tracks[1].valid);
}

/* -------------------------------------------------------------------------
 * Cartão removido durante a reprodução
 * ------------------------------------------------------------------------- */

ZTEST(library, test_remocao_esvazia_a_lista_e_desliga_o_transporte)
{
	insert_good_card();
	pull_card();

	zassert_equal(LIBRARY_REMOVED, library_state(),
		      "havia biblioteca: a tela diz removido, nao sem cartao");
	assert_no_library();
}

ZTEST(library, test_faixa_ja_carregada_sobrevive_a_remocao)
{
	int size;

	insert_good_card();

	size = library_load(0, buf, sizeof(buf), NULL);
	zassert_true(size > 0, "carga devolveu %d", size);

	pull_card();

	/* É o RNF07 visto daqui: a faixa em curso está no buffer de quem a
	 * pediu, e a remoção não encosta nele.
	 */
	zassert_mem_equal("Fur Elise:", buf, strlen("Fur Elise:"));
}

ZTEST(library, test_carga_depois_da_remocao_responde_sem_cartao)
{
	insert_good_card();
	pull_card();

	zassert_equal(-ENODEV, library_load(0, buf, sizeof(buf), NULL));
}

ZTEST(library, test_reinsercao_volta_a_lista)
{
	insert_good_card();
	pull_card();
	insert_good_card();

	zassert_equal(1, library_neighbor(0, true), "o transporte deveria ter voltado");
}

ZTEST(library, test_remocao_vista_pela_carga_nao_espera_o_monitor)
{
	insert_good_card();

	/* O cartão sai entre duas sondagens, e a carga chega primeiro. */
	storage_fake_set_present(false);

	zassert_equal(-ENODEV, library_load(1, buf, sizeof(buf), NULL));
	zassert_equal(LIBRARY_REMOVED, library_state());
	assert_no_library();
}

/* -------------------------------------------------------------------------
 * Cartão presente que não lê
 * ------------------------------------------------------------------------- */

ZTEST(library, test_primeiro_cartao_ilegivel)
{
	storage_fake_set_failing(true);

	zassert_equal(-EIO, library_refresh());
	zassert_equal(LIBRARY_UNREADABLE, library_state());
	assert_no_library();
}

ZTEST(library, test_ilegivel_depois_de_um_que_leu_pede_reinicio)
{
	/* A remontagem a quente da zephyr#94033: um cartão já leu nesta
	 * sessão, saiu, e o que entrou não leu.
	 */
	insert_good_card();
	pull_card();

	storage_fake_set_present(true);
	storage_fake_set_failing(true);

	zassert_equal(-EIO, library_refresh());
	zassert_equal(LIBRARY_REBOOT, library_state());
	zassert_str_equal("Reinicie a placa", library_state_text(library_state()));
	assert_no_library();
}

ZTEST(library, test_reinicio_pedido_nao_impede_nova_tentativa)
{
	/* Tentar custa pouco e às vezes funciona: tirar e pôr de novo um
	 * cartão que lê traz a lista de volta.
	 */
	insert_good_card();
	storage_fake_set_failing(true);
	zassert_equal(-EIO, library_refresh());
	zassert_equal(LIBRARY_REBOOT, library_state());

	pull_card();
	zassert_equal(LIBRARY_REMOVED, library_state());

	insert_good_card();
}

ZTEST(library, test_ilegivel_que_sai_e_sem_cartao)
{
	storage_fake_set_failing(true);
	zassert_equal(-EIO, library_refresh());

	pull_card();

	/* Nenhum cartão chegou a ler: não há o que dizer que foi removido. */
	zassert_equal(LIBRARY_NO_CARD, library_state());
}

ZTEST(library, test_erro_de_leitura_na_carga_pede_reinicio)
{
	insert_good_card();
	storage_fake_set_failing(true);

	zassert_equal(-EIO, library_load(0, buf, sizeof(buf), NULL));
	zassert_equal(LIBRARY_REBOOT, library_state());
	assert_no_library();
}

/* -------------------------------------------------------------------------
 * Carga fora de LIBRARY_READY
 * ------------------------------------------------------------------------- */

ZTEST(library, test_carga_sem_lista_nao_toca_no_cartao)
{
	/* Uma falha armada só é consumida por uma carga que chegue ao
	 * storage. Se ela continuar armada depois, ninguém chegou lá.
	 */
	storage_fake_fail_next_load(-EFBIG);

	zassert_equal(-ENODEV, library_load(0, buf, sizeof(buf), NULL));

	insert_good_card();
	zassert_equal(-EFBIG, library_load(0, buf, sizeof(buf), NULL),
		      "a carga sem lista tocou no storage");
}

ZTEST(library, test_carga_em_cada_estado_sem_lista)
{
	pull_card();
	zassert_equal(-ENODEV, library_load(0, buf, sizeof(buf), NULL), "sem cartao");

	insert_good_card();
	pull_card();
	zassert_equal(-ENODEV, library_load(0, buf, sizeof(buf), NULL), "removido");

	library_reset();
	storage_fake_set_present(true);
	storage_fake_set_failing(true);
	(void)library_refresh();
	zassert_equal(-EIO, library_load(0, buf, sizeof(buf), NULL), "ilegivel");

	storage_fake_set_failing(false);
	insert_good_card();
	storage_fake_set_failing(true);
	(void)library_refresh();
	zassert_equal(-EIO, library_load(0, buf, sizeof(buf), NULL), "reinicie");
}

/* -------------------------------------------------------------------------
 * Arquivo inválido
 * ------------------------------------------------------------------------- */

ZTEST(library, test_arquivo_grande_ganha_marca_e_a_lista_fica)
{
	insert_good_card();
	storage_fake_fail_next_load(-EFBIG);

	zassert_equal(-EFBIG, library_load(1, buf, sizeof(buf), NULL));

	/* Culpa do arquivo, não do cartão: a biblioteca segue de pé. */
	zassert_equal(LIBRARY_READY, library_state());
	zassert_equal(2, library_copy(tracks, ARRAY_SIZE(tracks), NULL));
	zassert_true(tracks[0].valid);
	zassert_false(tracks[1].valid, "a faixa grande deveria ter ganho o '!'");
}

ZTEST(library, test_arquivo_sumido_ganha_marca)
{
	insert_good_card();
	storage_fake_fail_next_load(-ENOENT);

	zassert_equal(-ENOENT, library_load(0, buf, sizeof(buf), NULL));

	zassert_equal(LIBRARY_READY, library_state());
	(void)library_copy(tracks, ARRAY_SIZE(tracks), NULL);
	zassert_false(tracks[0].valid);
	zassert_true(tracks[1].valid);
}

ZTEST(library, test_rejeicao_do_interpretador_marca_a_faixa)
{
	uint32_t generation;

	insert_good_card();

	zassert_true(library_load(0, buf, sizeof(buf), &generation) > 0);
	zassert_ok(library_reject(0, generation));

	(void)library_copy(tracks, ARRAY_SIZE(tracks), NULL);
	zassert_false(tracks[0].valid);
	zassert_equal(LIBRARY_READY, library_state());
}

ZTEST(library, test_rejeicao_com_lista_velha_nao_marca)
{
	uint32_t generation;

	insert_good_card();
	zassert_true(library_load(0, buf, sizeof(buf), &generation) > 0);

	/* Uma varredura entre a carga e a recusa: o índice 0 pode já ser
	 * outra faixa.
	 */
	(void)library_refresh();

	zassert_equal(-ESTALE, library_reject(0, generation));
	(void)library_copy(tracks, ARRAY_SIZE(tracks), NULL);
	zassert_true(tracks[0].valid, "o '!' foi parar na lista errada");
}

ZTEST(library, test_rejeicao_fora_da_lista)
{
	insert_good_card();

	zassert_equal(-ENOENT, library_reject(2, library_generation()));
	zassert_equal(-ENOENT, library_reject(-1, library_generation()));
}

ZTEST(library, test_erro_fora_do_contrato_nao_marca_nem_derruba)
{
	insert_good_card();
	storage_fake_fail_next_load(-EACCES);

	zassert_equal(-EACCES, library_load(0, buf, sizeof(buf), NULL));

	zassert_equal(LIBRARY_READY, library_state());
	(void)library_copy(tracks, ARRAY_SIZE(tracks), NULL);
	zassert_true(tracks[0].valid);
}

/* -------------------------------------------------------------------------
 * Carga: argumentos
 * ------------------------------------------------------------------------- */

ZTEST(library, test_carga_devolve_a_faixa_inteira)
{
	uint32_t generation = 0U;
	int size;

	insert_good_card();

	size = library_load(1, buf, sizeof(buf), &generation);

	zassert_true(size > 0);
	zassert_mem_equal("Ode to Joy:", buf, strlen("Ode to Joy:"));
	zassert_equal(library_generation(), generation);
}

ZTEST(library, test_carga_com_buffer_menor_que_o_teto_e_recusada)
{
	/* Com menos de 4 KB, um -EFBIG seria culpa do buffer, e a faixa
	 * ganharia um '!' que não merece.
	 */
	insert_good_card();

	zassert_equal(-EINVAL, library_load(0, buf, sizeof(buf) - 1U, NULL));
	zassert_equal(-EINVAL, library_load(0, NULL, sizeof(buf), NULL));
}

ZTEST(library, test_carga_fora_da_lista_nao_marca_nada)
{
	insert_good_card();

	zassert_equal(-ENOENT, library_load(2, buf, sizeof(buf), NULL));
	zassert_equal(-ENOENT, library_load(-1, buf, sizeof(buf), NULL));

	(void)library_copy(tracks, ARRAY_SIZE(tracks), NULL);
	zassert_true(tracks[0].valid && tracks[1].valid);
}

/* -------------------------------------------------------------------------
 * Seguinte e anterior
 * ------------------------------------------------------------------------- */

ZTEST(library, test_seguinte_e_anterior_dao_a_volta)
{
	insert_good_card();

	zassert_equal(1, library_neighbor(0, true));
	zassert_equal(0, library_neighbor(1, true));
	zassert_equal(1, library_neighbor(0, false));
	zassert_equal(0, library_neighbor(1, false));
}

ZTEST(library, test_sem_faixa_de_partida_comeca_de_uma_ponta)
{
	insert_good_card();

	zassert_equal(0, library_neighbor(TRACK_NONE, true));
	zassert_equal(1, library_neighbor(TRACK_NONE, false));
	zassert_equal(0, library_neighbor((int)LIBRARY_MAX, true));
	zassert_equal(1, library_neighbor((int)LIBRARY_MAX, false));
}

ZTEST(library, test_faixa_invalida_e_pulada)
{
	uint32_t generation;

	insert_good_card();
	(void)library_load(1, buf, sizeof(buf), &generation);
	zassert_ok(library_reject(1, generation));

	/* Só a faixa 0 sobrou: seguinte dela é recomeçar. */
	zassert_equal(0, library_neighbor(0, true));
	zassert_equal(0, library_neighbor(0, false));
	zassert_equal(0, library_neighbor(TRACK_NONE, false));
}

ZTEST(library, test_todas_invalidas_desliga_o_transporte)
{
	uint32_t generation;

	insert_good_card();
	(void)library_load(0, buf, sizeof(buf), &generation);
	zassert_ok(library_reject(0, generation));
	zassert_ok(library_reject(1, generation));

	zassert_equal(TRACK_NONE, library_neighbor(0, true));
	zassert_equal(TRACK_NONE, library_neighbor(TRACK_NONE, false));
}

/* -------------------------------------------------------------------------
 * Lista publicada
 * ------------------------------------------------------------------------- */

ZTEST(library, test_copia_valida_os_argumentos)
{
	size_t skipped = 99U;

	insert_good_card();

	zassert_equal(-EINVAL, library_copy(NULL, ARRAY_SIZE(tracks), &skipped));
	zassert_equal(0U, skipped, "skipped tem de sair zerado mesmo no erro");
	zassert_equal(-EINVAL, library_copy(tracks, 0U, NULL));
}

ZTEST(library, test_copia_respeita_a_capacidade)
{
	size_t skipped = 99U;

	insert_good_card();

	zassert_equal(1, library_copy(tracks, 1U, &skipped));
	zassert_str_equal("Fur Elise", tracks[0].name);
	zassert_equal(0U, skipped);
}

ZTEST(library, test_toda_varredura_muda_a_geracao)
{
	uint32_t before = library_generation();

	insert_good_card();
	zassert_not_equal(before, library_generation());

	/* Mesmo estado, mesma contagem: a lista ainda pode ser outra. */
	before = library_generation();
	insert_good_card();
	zassert_not_equal(before, library_generation());
}

/* -------------------------------------------------------------------------
 * Textos da tela de erro
 * ------------------------------------------------------------------------- */

ZTEST(library, test_cada_estado_tem_texto_proprio)
{
	static const enum library_state states[] = {
		LIBRARY_NO_CARD, LIBRARY_READY,	 LIBRARY_REMOVED,
		LIBRARY_UNREADABLE, LIBRARY_REBOOT,
	};

	for (size_t i = 0U; i < ARRAY_SIZE(states); i++) {
		const char *text = library_state_text(states[i]);

		/* 16 colunas com a UNSCII-8. */
		zassert_true(strlen(text) <= 16U, "\"%s\" nao cabe numa linha", text);

		for (size_t j = 0U; j < i; j++) {
			zassert_true(strcmp(text, library_state_text(states[j])) != 0,
				     "dois estados com o mesmo texto: \"%s\"", text);
		}
	}
}

ZTEST(library, test_estado_desconhecido_tem_texto_generico)
{
	zassert_str_equal("Erro inesperado", library_state_text((enum library_state)99));
}

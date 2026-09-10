/**
 * @file main.c
 * @brief Testes de @ref storage.h sobre a implementação de mentira.
 *
 * Os casos interessantes são os de falha, e é por eles que a implementação
 * de mentira existe: "sem cartão" e "cartão que não responde" não se
 * reproduzem com hardware de verdade sem alguém puxar o cartão no
 * milissegundo certo.
 *
 * Quando esta implementação for trocada pela do cartão (issue #9), este
 * arquivo continua descrevendo o contrato — o que muda é quem o cumpre.
 */

#include <vitrolinha/storage.h>
#include <vitrolinha/storage_fake.h>
#include <vitrolinha/track.h>

#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

/** @brief Cada caso começa com cartão presente e saudável. */
static void storage_before(void *fixture)
{
	ARG_UNUSED(fixture);
	storage_fake_reset();
}

ZTEST_SUITE(storage, NULL, NULL, storage_before, NULL, NULL);

/* -------------------------------------------------------------------------
 * Presença
 * ------------------------------------------------------------------------- */

ZTEST(storage, test_cartao_presente_por_padrao)
{
	zassert_true(storage_present());
}

ZTEST(storage, test_ausencia_de_cartao_e_visivel)
{
	storage_fake_set_present(false);
	zassert_false(storage_present());

	storage_fake_set_present(true);
	zassert_true(storage_present());
}

ZTEST(storage, test_cartao_mudo_continua_presente)
{
	/* Distinção que importa para a tela: "sem cartao" e "cartao ilegivel"
	 * pedem mensagens diferentes.
	 */
	storage_fake_set_failing(true);

	zassert_true(storage_present());
}

/* -------------------------------------------------------------------------
 * Varredura
 * ------------------------------------------------------------------------- */

ZTEST(storage, test_varredura_devolve_as_faixas_embutidas)
{
	struct track_meta library[LIBRARY_MAX];
	int count = storage_scan(library, ARRAY_SIZE(library));

	zassert_equal((int)STORAGE_FAKE_TRACK_COUNT, count);
	zassert_str_equal("Fur Elise", library[0].name);
	zassert_str_equal("Ode to Joy", library[1].name);
}

ZTEST(storage, test_varredura_numera_as_faixas_em_ordem)
{
	struct track_meta library[LIBRARY_MAX];
	int count = storage_scan(library, ARRAY_SIZE(library));

	for (int i = 0; i < count; i++) {
		zassert_equal((uint8_t)i, library[i].index,
			      "o indice tem de casar com a posicao");
		zassert_true(library[i].valid);
	}
}

ZTEST(storage, test_varredura_respeita_a_capacidade_do_chamador)
{
	struct track_meta library[1];

	zassert_equal(1, storage_scan(library, ARRAY_SIZE(library)));
	zassert_str_equal("Fur Elise", library[0].name);
}

ZTEST(storage, test_varredura_limita_ao_teto_da_biblioteca)
{
	struct track_meta library[LIBRARY_MAX];

	/* Capacidade declarada acima do teto não pode fazer a varredura
	 * escrever além de LIBRARY_MAX: é o teto que mantém o consumo de RAM
	 * previsível (RNF06).
	 */
	zassert_equal((int)STORAGE_FAKE_TRACK_COUNT,
		      storage_scan(library, LIBRARY_MAX * 4U));
}

ZTEST(storage, test_varredura_recusa_argumentos_invalidos)
{
	struct track_meta library[LIBRARY_MAX];

	zassert_equal(-EINVAL, storage_scan(NULL, ARRAY_SIZE(library)));
	zassert_equal(-EINVAL, storage_scan(library, 0U));
}

ZTEST(storage, test_varredura_sem_cartao)
{
	struct track_meta library[LIBRARY_MAX];

	storage_fake_set_present(false);

	zassert_equal(-ENODEV, storage_scan(library, ARRAY_SIZE(library)));
}

ZTEST(storage, test_varredura_com_cartao_mudo)
{
	struct track_meta library[LIBRARY_MAX];

	storage_fake_set_failing(true);

	zassert_equal(-EIO, storage_scan(library, ARRAY_SIZE(library)));
}

ZTEST(storage, test_ausencia_tem_precedencia_sobre_falha)
{
	struct track_meta library[LIBRARY_MAX];

	/* Com os dois estados ligados, a mensagem certa na tela é "sem
	 * cartao": não faz sentido reclamar de leitura num soquete vazio.
	 */
	storage_fake_set_present(false);
	storage_fake_set_failing(true);

	zassert_equal(-ENODEV, storage_scan(library, ARRAY_SIZE(library)));
}

/* -------------------------------------------------------------------------
 * Carga
 * ------------------------------------------------------------------------- */

ZTEST(storage, test_carga_traz_o_arquivo_inteiro)
{
	uint8_t buf[STORAGE_FILE_MAX];
	int size = storage_load(0, buf, sizeof(buf));

	zassert_true(size > 0, "carga devolveu %d", size);
	zassert_mem_equal("Fur Elise:", buf, 10U,
			  "o conteudo tem de comecar pelo cabecalho RTTTL");
}

ZTEST(storage, test_carga_de_cada_faixa_da_biblioteca)
{
	uint8_t buf[STORAGE_FILE_MAX];

	for (int i = 0; i < (int)STORAGE_FAKE_TRACK_COUNT; i++) {
		zassert_true(storage_load(i, buf, sizeof(buf)) > 0,
			     "faixa %d nao carregou", i);
	}
}

ZTEST(storage, test_carga_recusa_argumentos_invalidos)
{
	uint8_t buf[STORAGE_FILE_MAX];

	zassert_equal(-EINVAL, storage_load(0, NULL, sizeof(buf)));
	zassert_equal(-EINVAL, storage_load(0, buf, 0U));
}

ZTEST(storage, test_carga_de_faixa_inexistente)
{
	uint8_t buf[STORAGE_FILE_MAX];

	zassert_equal(-ENOENT, storage_load(-1, buf, sizeof(buf)));
	zassert_equal(-ENOENT,
		      storage_load((int)STORAGE_FAKE_TRACK_COUNT, buf, sizeof(buf)));
	zassert_equal(-ENOENT, storage_load((int)LIBRARY_MAX, buf, sizeof(buf)));
}

ZTEST(storage, test_carga_rejeita_arquivo_maior_que_o_buffer)
{
	uint8_t buf[8];

	/* Rejeitar, não truncar: meia melodia sem aviso é pior do que um erro
	 * na tela.
	 */
	zassert_equal(-EFBIG, storage_load(0, buf, sizeof(buf)));
}

ZTEST(storage, test_carga_nao_escreve_ao_rejeitar)
{
	uint8_t buf[8];

	(void)memset(buf, 0xAA, sizeof(buf));
	zassert_equal(-EFBIG, storage_load(0, buf, sizeof(buf)));

	for (size_t i = 0U; i < sizeof(buf); i++) {
		zassert_equal(0xAA, buf[i], "buffer sujo na posicao %zu", i);
	}
}

ZTEST(storage, test_carga_sem_cartao)
{
	uint8_t buf[STORAGE_FILE_MAX];

	storage_fake_set_present(false);

	zassert_equal(-ENODEV, storage_load(0, buf, sizeof(buf)));
}

ZTEST(storage, test_carga_com_cartao_mudo)
{
	uint8_t buf[STORAGE_FILE_MAX];

	storage_fake_set_failing(true);

	zassert_equal(-EIO, storage_load(0, buf, sizeof(buf)));
}

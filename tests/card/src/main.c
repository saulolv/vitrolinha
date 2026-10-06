/**
 * @file main.c
 * @brief Testes de @ref card.h com um cartão que o teste põe e tira.
 *
 * O critério da issue #9 é "inserir e remover o cartão é detectado
 * corretamente". É uma afirmação sobre um evento físico, e é por isso que
 * existe o disco de mentira de `fake_disk.h`: sem ele, conferir isto
 * dependeria de alguém puxar o cartão da bancada na hora certa, e não valeria
 * como regressão automática nenhuma vez.
 *
 * O que estes testes exercitam é o módulo de verdade sobre o FatFs de
 * verdade — `fs_mount`, `fs_unmount` e `disk_access` são os mesmos da placa.
 * O que muda é de onde vêm os setores.
 */

#include "fake_disk.h"

#include <vitrolinha/card.h>

#include <errno.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/**
 * @brief Quanto esperar por uma transição do monitor.
 *
 * O monitor sonda a cada 250 ms. Um segundo dá quatro sondagens de folga, e
 * em `native_sim` o relógio é simulado: a espera não custa tempo de parede.
 */
#define SETTLE_TIMEOUT_MS 1000

/** @brief Passo da espera. Bem abaixo do intervalo do monitor. */
#define SETTLE_STEP_MS 25

/** @brief O que @ref card_init devolveu na primeira e única chamada. */
static int init_result;

/** @brief Quantas vezes o observador foi chamado desde o último preparo. */
static int observed_calls;

/** @brief O último estado que o observador viu. */
static enum card_state observed_state;

static void on_card_change(enum card_state state)
{
	observed_calls++;
	observed_state = state;
}

/**
 * @brief Espera o monitor concluir @p expected, ou desiste.
 *
 * @return O estado em que o cartão parou.
 */
static enum card_state settle(enum card_state expected)
{
	int64_t deadline = k_uptime_get() + SETTLE_TIMEOUT_MS;

	while ((card_get_state() != expected) && (k_uptime_get() < deadline)) {
		k_msleep(SETTLE_STEP_MS);
	}

	return card_get_state();
}

static void *card_setup(void)
{
	zassert_ok(fake_disk_setup(), "o disco de mentira nao subiu");

	card_observe(on_card_change);
	init_result = card_init();

	return NULL;
}

/**
 * @brief Todo caso começa com o soquete vazio, sem falhas e sem histórico.
 *
 * A volta ao estado inicial é feita por @ref card_refresh e não esperando o
 * monitor: o preparo não é o que se está medindo, e chamar direto torna o
 * ponto de partida determinístico.
 */
static void card_before(void *fixture)
{
	ARG_UNUSED(fixture);

	card_observe(on_card_change);
	fake_disk_reset();

	zassert_equal(CARD_ABSENT, card_refresh(), "preparo nao devolveu ao soquete vazio");

	observed_calls = 0;
	observed_state = CARD_ABSENT;
}

ZTEST_SUITE(card, NULL, card_setup, card_before, NULL, NULL);

/* -------------------------------------------------------------------------
 * Inicialização
 * ------------------------------------------------------------------------- */

ZTEST(card, test_init_sobe_o_monitor)
{
	zassert_ok(init_result, "card_init devolveu %d", init_result);
}

ZTEST(card, test_init_nao_cria_um_segundo_monitor)
{
	/* Duas threads sondando o mesmo soquete e montando o mesmo volume
	 * seria o pior tipo de bug: intermitente e só na placa.
	 */
	zassert_equal(-EALREADY, card_init());
}

ZTEST(card, test_soquete_vazio_e_ausente)
{
	zassert_equal(CARD_ABSENT, card_get_state());
	zassert_false(card_ready());
}

ZTEST(card, test_sem_disco_registrado_o_cartao_e_ausente)
{
	/* É a situação do alvo de simulação do firmware: nenhum nó
	 * `zephyr,sdmmc-disk` no devicetree, `disk_access_status` devolvendo
	 * erro. Tem de virar "ausente", e não falha de inicialização.
	 */
	fake_disk_register(false);
	fake_disk_insert(true);

	zassert_equal(CARD_ABSENT, card_refresh());
	zassert_false(card_ready());
}

/* -------------------------------------------------------------------------
 * Inserção e remoção
 * ------------------------------------------------------------------------- */

ZTEST(card, test_insercao_e_detectada_pelo_monitor)
{
	fake_disk_insert(true);

	zassert_equal(CARD_READY, settle(CARD_READY), "o monitor nao viu a insercao");
	zassert_true(card_ready());
	zassert_equal(1, observed_calls, "o observador tinha de ser avisado uma vez");
	zassert_equal(CARD_READY, observed_state);
}

ZTEST(card, test_remocao_e_detectada_pelo_monitor)
{
	fake_disk_insert(true);
	zassert_equal(CARD_READY, settle(CARD_READY));

	fake_disk_insert(false);

	zassert_equal(CARD_ABSENT, settle(CARD_ABSENT), "o monitor nao viu a remocao");
	zassert_false(card_ready());
	zassert_equal(2, observed_calls, "insercao e remocao, uma notificacao cada");
	zassert_equal(CARD_ABSENT, observed_state);
}

ZTEST(card, test_cartao_parado_nao_gera_notificacao)
{
	fake_disk_insert(true);
	zassert_equal(CARD_READY, settle(CARD_READY));

	/* Várias sondagens do monitor com o cartão no mesmo estado. O
	 * observador é de transição, não de amostragem: a interface não pode
	 * redesenhar a tela quatro vezes por segundo à toa.
	 */
	k_msleep(SETTLE_TIMEOUT_MS);

	zassert_equal(1, observed_calls, "notificou %d vezes sem nada mudar",
		      observed_calls);
	zassert_equal(CARD_READY, card_get_state());
}

ZTEST(card, test_refresh_nao_espera_a_cadencia_do_monitor)
{
	fake_disk_insert(true);

	/* Sem dormir: quem chama pode forçar a avaliação em vez de esperar os
	 * 250 ms da sondagem.
	 */
	zassert_equal(CARD_READY, card_refresh());
}

/* -------------------------------------------------------------------------
 * Cartão presente e ilegível
 * ------------------------------------------------------------------------- */

ZTEST(card, test_cartao_que_nao_inicializa_fica_ilegivel)
{
	fake_disk_fail_init(true);
	fake_disk_insert(true);

	/* Presente e ilegível, não ausente: o soquete não está vazio, e a
	 * mensagem na tela é outra.
	 */
	zassert_equal(CARD_UNREADABLE, settle(CARD_UNREADABLE));
	zassert_false(card_ready());
}

ZTEST(card, test_volume_que_nao_monta_fica_ilegivel)
{
	fake_disk_fail_read(true);
	fake_disk_insert(true);

	zassert_equal(CARD_UNREADABLE, settle(CARD_UNREADABLE));
	zassert_false(card_ready());
}

ZTEST(card, test_ilegivel_nao_e_tentado_de_novo_no_soquete)
{
	fake_disk_fail_read(true);
	fake_disk_insert(true);
	zassert_equal(CARD_UNREADABLE, settle(CARD_UNREADABLE));

	/* O cartão "melhora" sem sair do soquete. Um volume não se conserta
	 * sozinho, então o módulo não tem por que descobrir isso: insistir a
	 * cada 250 ms gastaria barramento para sempre.
	 */
	fake_disk_fail_read(false);
	k_msleep(SETTLE_TIMEOUT_MS);

	zassert_equal(CARD_UNREADABLE, card_get_state(),
		      "voltou a tentar montar sem o cartao ter saido");
}

ZTEST(card, test_reinsercao_tenta_montar_de_novo)
{
	fake_disk_fail_read(true);
	fake_disk_insert(true);
	zassert_equal(CARD_UNREADABLE, settle(CARD_UNREADABLE));

	/* Tirar e pôr é o gesto que pede nova tentativa — e é o único. */
	fake_disk_insert(false);
	zassert_equal(CARD_ABSENT, settle(CARD_ABSENT));

	fake_disk_fail_read(false);
	fake_disk_insert(true);

	zassert_equal(CARD_READY, settle(CARD_READY), "a reinsercao nao remontou");
}

ZTEST(card, test_cartao_ruim_seguido_de_bom_monta)
{
	/* A armadilha que este caso guarda: quando `fs_mount` falha DEPOIS de
	 * inicializar o cartão, ninguém desfaz essa inicialização, e a pilha
	 * SD segue achando que o cartão está pronto. Sem o release explícito,
	 * o FatFs pularia a inicialização do cartão seguinte e leria lixo.
	 */
	fake_disk_fail_read(true);
	fake_disk_insert(true);
	zassert_equal(CARD_UNREADABLE, settle(CARD_UNREADABLE));

	fake_disk_insert(false);
	zassert_equal(CARD_ABSENT, settle(CARD_ABSENT));

	fake_disk_fail_read(false);
	fake_disk_insert(true);
	zassert_equal(CARD_READY, settle(CARD_READY));

	/* E o cartão bom tem de continuar montável depois de uma volta
	 * inteira, o que só acontece se a contagem de referências do disco
	 * tiver fechado.
	 */
	fake_disk_insert(false);
	zassert_equal(CARD_ABSENT, settle(CARD_ABSENT));

	fake_disk_insert(true);
	zassert_equal(CARD_READY, settle(CARD_READY));
}

/* -------------------------------------------------------------------------
 * Remoção a quente
 * ------------------------------------------------------------------------- */

ZTEST(card, test_desmontagem_que_falha_nao_prende_o_cartao)
{
	fake_disk_insert(true);
	zassert_equal(CARD_READY, settle(CARD_READY));

	/* A desmontagem que não completa é o que a issue zephyr#94033
	 * descreve, fechada como "not planned". O módulo tem de seguir como
	 * desmontado: continuar achando que o volume está montado bloquearia
	 * para sempre a próxima montagem.
	 */
	fake_disk_fail_deinit(true);
	fake_disk_insert(false);

	zassert_equal(CARD_ABSENT, settle(CARD_ABSENT));

	fake_disk_fail_deinit(false);
	fake_disk_insert(true);

	zassert_equal(CARD_READY, settle(CARD_READY),
		      "ficou preso achando que o volume continuava montado");
}

/* -------------------------------------------------------------------------
 * Empréstimo do volume
 * ------------------------------------------------------------------------- */

ZTEST(card, test_emprestimo_com_soquete_vazio)
{
	/* Nada a emprestar, e nada a devolver: quem recebe erro não chama
	 * card_release. É daqui que o storage tira o -ENODEV do contrato.
	 */
	zassert_equal(-ENODEV, card_acquire());
}

ZTEST(card, test_emprestimo_com_volume_ilegivel)
{
	fake_disk_fail_read(true);
	fake_disk_insert(true);
	zassert_equal(CARD_UNREADABLE, settle(CARD_UNREADABLE));

	/* Presente e ilegível pede outra mensagem na tela que soquete vazio,
	 * e o código de erro é o que carrega a diferença.
	 */
	zassert_equal(-EIO, card_acquire());
}

ZTEST(card, test_emprestimo_com_cartao_pronto)
{
	fake_disk_insert(true);
	zassert_equal(CARD_READY, settle(CARD_READY));

	zassert_ok(card_acquire());
	card_release();
}

ZTEST(card, test_monitor_nao_desmonta_durante_o_emprestimo)
{
	fake_disk_insert(true);
	zassert_equal(CARD_READY, settle(CARD_READY));

	zassert_ok(card_acquire());

	/* O cartão sai no meio de uma leitura. O FatFs roda sem reentrância:
	 * desmontar agora zeraria o objeto de trabalho do volume por baixo de
	 * quem o está usando. O monitor tem de esperar a devolução.
	 */
	fake_disk_insert(false);
	k_msleep(SETTLE_TIMEOUT_MS);

	zassert_equal(CARD_READY, card_get_state(),
		      "o monitor desmontou um volume emprestado");

	card_release();

	zassert_equal(CARD_ABSENT, settle(CARD_ABSENT),
		      "a remocao nao foi vista depois da devolucao");
}

/* -------------------------------------------------------------------------
 * Observador
 * ------------------------------------------------------------------------- */

ZTEST(card, test_observador_pode_ser_desligado)
{
	card_observe(NULL);

	fake_disk_insert(true);

	zassert_equal(CARD_READY, settle(CARD_READY), "o monitor parou junto com o aviso");
	zassert_equal(0, observed_calls, "avisou mesmo depois de desligado");
}

/* -------------------------------------------------------------------------
 * Nomes
 * ------------------------------------------------------------------------- */

ZTEST(card, test_nome_de_cada_estado)
{
	/* Os três nomes do CONTEXT.md, que é o vocabulário que vai para a
	 * tela e para o relatório.
	 */
	zassert_str_equal("ausente", card_state_name(CARD_ABSENT));
	zassert_str_equal("presente e legivel", card_state_name(CARD_READY));
	zassert_str_equal("presente e ilegivel", card_state_name(CARD_UNREADABLE));
}

ZTEST(card, test_nome_de_estado_fora_da_enumeracao)
{
	/* Vale mais um texto inútil no log do que uma leitura fora da tabela
	 * por causa de um valor que não deveria existir.
	 */
	zassert_str_equal("desconhecido", card_state_name((enum card_state)99));
}

ZTEST(card, test_ponto_de_montagem_e_o_volume_do_fatfs)
{
	/* A barra é do subsistema de arquivos, os dois-pontos são do FatFs, e
	 * o nome no meio é o do disco.
	 */
	zassert_str_equal("SD", CARD_DISK_NAME);
	zassert_str_equal("/SD:", CARD_MOUNT_POINT);
}

ZTEST(card, test_volume_montado_e_navegavel)
{
	struct fs_dir_t dir;

	/* Montar não basta: sem os dois-pontos no ponto de montagem, o
	 * fs_mount dá certo e todo caminho dentro do volume falha. Este é o
	 * caso que só um acesso de verdade pega.
	 */
	fake_disk_insert(true);
	zassert_equal(CARD_READY, settle(CARD_READY));

	fs_dir_t_init(&dir);
	zassert_ok(fs_opendir(&dir, CARD_MOUNT_POINT), "o volume montou mas nao abre");
	zassert_ok(fs_closedir(&dir));
}

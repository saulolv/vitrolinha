/**
 * @file main.c
 * @brief A matriz de falhas da issue #12, de ponta a ponta, sobre o cartão.
 *
 * tests/library/ confere a lógica da matriz sobre a implementação de mentira
 * do storage. Aqui a pilha inteira é a do firmware — biblioteca, storage do
 * cartão, módulo `card` com o seu monitor, FatFs —, e só o disco embaixo é de
 * mentira. As falhas são induzidas como na bancada: o cartão sai do soquete,
 * volta, deixa de ler, e um arquivo some.
 *
 * O observador é o mesmo do `main.c`: toda transição do cartão vira uma
 * varredura da biblioteca. Nos casos em que o teste espera o monitor em vez
 * de chamar @ref card_refresh, é a thread do monitor que varre, como na
 * placa.
 */

#include "fake_disk.h"

#include <vitrolinha/card.h>
#include <vitrolinha/library.h>
#include <vitrolinha/storage.h>
#include <vitrolinha/track.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

/**
 * @brief Quanto esperar o monitor levar uma transição até a biblioteca.
 *
 * O monitor sonda a cada 250 ms; um segundo dá quatro sondagens de folga, e
 * em `native_sim` o relógio é simulado.
 */
#define SETTLE_TIMEOUT_MS 1000

/** @brief Passo da espera. Bem abaixo do intervalo do monitor. */
#define SETTLE_STEP_MS 25

/** @brief Quantas entradas a limpeza da raiz consegue apagar. */
#define WIPE_MAX 8U

/** @brief Um cabeçalho RTTTL mínimo e válido, depois do nome. */
#define BODY ":d=4,o=5,b=125:c,d,e"

/** @brief Destino das cargas, do tamanho que o contrato exige. */
static uint8_t buf[STORAGE_FILE_MAX];

/** @brief Destino das cópias da lista. */
static struct track_meta tracks[LIBRARY_MAX];

/** @brief O observador do firmware: toda transição varre a biblioteca. */
static void on_card_change(enum card_state state)
{
	ARG_UNUSED(state);

	(void)library_refresh();
}

/** @brief Escreve um arquivo de texto na raiz do cartão montado. */
static void put_text(const char *name, const char *text)
{
	char path[32];
	struct fs_file_t file;
	size_t len = strlen(text);

	(void)snprintf(path, sizeof(path), "%s/%s", CARD_MOUNT_POINT, name);
	fs_file_t_init(&file);

	zassert_ok(fs_open(&file, path, FS_O_CREATE | FS_O_WRITE), "nao abriu %s", path);
	zassert_equal((ssize_t)len, fs_write(&file, text, len), "nao escreveu %s", path);
	zassert_ok(fs_close(&file));
}

/** @brief Apaga um arquivo da raiz do cartão montado. */
static void remove_file(const char *name)
{
	char path[32];

	(void)snprintf(path, sizeof(path), "%s/%s", CARD_MOUNT_POINT, name);
	zassert_ok(fs_unlink(path), "nao apagou %s", path);
}

/**
 * @brief Apaga tudo o que houver na raiz do cartão montado.
 *
 * Os nomes são juntados antes de apagar: remover entradas do diretório
 * enquanto ele é percorrido é pedir para pular uma.
 */
static void wipe_root(void)
{
	static char names[WIPE_MAX][sizeof(((struct fs_dirent *)0)->name)];
	struct fs_dir_t dir;
	struct fs_dirent entry;
	size_t count = 0U;

	fs_dir_t_init(&dir);
	zassert_ok(fs_opendir(&dir, CARD_MOUNT_POINT));

	while ((fs_readdir(&dir, &entry) == 0) && (entry.name[0] != '\0')) {
		zassert_true(count < WIPE_MAX, "raiz com entradas demais para limpar");
		(void)strcpy(names[count], entry.name);
		count++;
	}

	zassert_ok(fs_closedir(&dir));

	for (size_t i = 0U; i < count; i++) {
		remove_file(names[i]);
	}
}

/** @brief Espera a biblioteca chegar a @p expected, ou desiste. */
static enum library_state settle(enum library_state expected)
{
	int64_t deadline = k_uptime_get() + SETTLE_TIMEOUT_MS;

	while ((library_state() != expected) && (k_uptime_get() < deadline)) {
		k_msleep(SETTLE_STEP_MS);
	}

	return library_state();
}

/**
 * @brief Prepara o conteúdo do cartão e volta ao boot.
 *
 * Escrever exige o cartão montado, e montar faz a biblioteca varrer e
 * lembrar que um cartão já leu. Por isso, escrito o conteúdo, o cartão sai do
 * soquete e a biblioteca esquece: o caso começa como quem acabou de ligar a
 * placa, com o cartão pronto na mão.
 *
 * @param fill Escreve os arquivos do caso.
 */
static void prepare_card(void (*fill)(void))
{
	fake_disk_insert(true);
	zassert_equal(CARD_READY, card_refresh(), "o cartao nao montou para o preparo");

	wipe_root();
	fill();

	fake_disk_insert(false);
	zassert_equal(CARD_ABSENT, card_refresh(), "o soquete nao esvaziou");

	library_reset();
}

/** @brief Põe o cartão e espera o monitor levar a lista até a biblioteca. */
static void insert_and_settle(void)
{
	fake_disk_insert(true);
	zassert_equal(LIBRARY_READY, settle(LIBRARY_READY), "a insercao nao chegou a biblioteca");
}

/** @brief Duas faixas válidas. */
static void two_tracks(void)
{
	put_text("FURELIS.TXT", "Fur Elise" BODY);
	put_text("ODE.TXT", "Ode to Joy" BODY);
}

/** @brief Uma faixa vazia entre duas válidas. */
static void hole_in_the_middle(void)
{
	put_text("A.TXT", "Primeira" BODY);
	put_text("NADA.TXT", "");
	put_text("C.TXT", "Terceira" BODY);
}

static void *library_card_setup(void)
{
	zassert_ok(fake_disk_setup(), "o disco de mentira nao subiu");
	zassert_ok(card_init(), "o monitor do cartao nao subiu");

	return NULL;
}

/** @brief Todo caso começa com o soquete vazio, sem falhas e no boot. */
static void library_card_before(void *fixture)
{
	ARG_UNUSED(fixture);

	card_observe(on_card_change);
	fake_disk_reset();
	zassert_equal(CARD_ABSENT, card_refresh(), "preparo nao devolveu ao soquete vazio");
	library_reset();
}

/** @brief Nenhum caso deixa o observador ligado para o próximo preparo. */
static void library_card_after(void *fixture)
{
	ARG_UNUSED(fixture);

	card_observe(NULL);
}

ZTEST_SUITE(library_card, NULL, library_card_setup, library_card_before, library_card_after,
	    NULL);

/* -------------------------------------------------------------------------
 * Sem cartão no boot
 * ------------------------------------------------------------------------- */

ZTEST(library_card, test_boot_sem_cartao_e_insercao_vista_pelo_monitor)
{
	prepare_card(two_tracks);

	zassert_equal(LIBRARY_NO_CARD, library_state());
	zassert_str_equal("Sem cartao", library_state_text(library_state()));

	/* Ninguém chama card_refresh: é o monitor que vê o cartão entrar,
	 * monta, e varre a biblioteca pelo observador.
	 */
	insert_and_settle();

	zassert_equal(2, library_copy(tracks, ARRAY_SIZE(tracks), NULL));
	zassert_str_equal("Fur Elise", tracks[0].name);
	zassert_str_equal("Ode to Joy", tracks[1].name);
}

/* -------------------------------------------------------------------------
 * Cartão removido durante a reprodução
 * ------------------------------------------------------------------------- */

ZTEST(library_card, test_remocao_durante_a_reproducao)
{
	int size;

	prepare_card(two_tracks);
	insert_and_settle();

	size = library_load(0, buf, sizeof(buf), NULL);
	zassert_true(size > 0, "carga devolveu %d", size);

	/* A faixa está em RAM. O cartão sai, e o monitor leva a notícia. */
	fake_disk_insert(false);
	zassert_equal(LIBRARY_REMOVED, settle(LIBRARY_REMOVED));
	zassert_str_equal("Cartao removido", library_state_text(library_state()));

	/* A faixa em curso continua inteira no buffer de quem a tocava. */
	zassert_mem_equal("Fur Elise" BODY, buf, strlen("Fur Elise" BODY));

	/* Seguinte e anterior desligados, e nenhuma carga chega ao cartão. */
	zassert_equal(TRACK_NONE, library_neighbor(0, true));
	zassert_equal(TRACK_NONE, library_neighbor(0, false));
	zassert_equal(0, library_copy(tracks, ARRAY_SIZE(tracks), NULL));
	zassert_equal(-ENODEV, library_load(1, buf, sizeof(buf), NULL));
}

ZTEST(library_card, test_reinsercao_que_monta_traz_a_lista_de_volta)
{
	prepare_card(two_tracks);
	insert_and_settle();

	fake_disk_insert(false);
	zassert_equal(LIBRARY_REMOVED, settle(LIBRARY_REMOVED));

	insert_and_settle();

	zassert_equal(1, library_neighbor(0, true));
}

/* -------------------------------------------------------------------------
 * Cartão presente que não lê
 * ------------------------------------------------------------------------- */

ZTEST(library_card, test_primeiro_cartao_ilegivel)
{
	prepare_card(two_tracks);

	fake_disk_fail_read(true);
	fake_disk_insert(true);
	zassert_equal(CARD_UNREADABLE, card_refresh());

	zassert_equal(LIBRARY_UNREADABLE, library_state());
	zassert_str_equal("Cartao ilegivel", library_state_text(library_state()));
}

ZTEST(library_card, test_reinsercao_que_nao_monta_pede_reinicio)
{
	/* A zephyr#94033 reproduzida: um cartão leu, saiu, e o que voltou
	 * não monta.
	 */
	prepare_card(two_tracks);
	zassert_equal(CARD_ABSENT, card_get_state());

	fake_disk_insert(true);
	zassert_equal(CARD_READY, card_refresh());
	zassert_equal(LIBRARY_READY, library_state());

	fake_disk_insert(false);
	zassert_equal(CARD_ABSENT, card_refresh());

	fake_disk_fail_read(true);
	fake_disk_insert(true);
	zassert_equal(CARD_UNREADABLE, card_refresh());

	zassert_equal(LIBRARY_REBOOT, library_state());
	zassert_equal(-EIO, library_load(0, buf, sizeof(buf), NULL));
}

ZTEST(library_card, test_cartao_que_para_de_ler_tocando_pede_reinicio)
{
	prepare_card(two_tracks);
	insert_and_settle();

	/* O volume segue montado e o soquete, ocupado: o monitor não vê
	 * transição nenhuma. Quem descobre é a carga da faixa seguinte.
	 */
	fake_disk_fail_read(true);

	zassert_equal(-EIO, library_load(1, buf, sizeof(buf), NULL));
	zassert_equal(LIBRARY_REBOOT, library_state());
	zassert_equal(TRACK_NONE, library_neighbor(1, true));
}

/* -------------------------------------------------------------------------
 * Arquivo inválido
 * ------------------------------------------------------------------------- */

ZTEST(library_card, test_faixa_recusada_na_varredura_e_pulada)
{
	prepare_card(hole_in_the_middle);
	insert_and_settle();

	zassert_equal(3, library_copy(tracks, ARRAY_SIZE(tracks), NULL));
	zassert_false(tracks[1].valid, "o arquivo vazio deveria vir com '!'");

	/* O transporte passa por cima do '!'. */
	zassert_equal(2, library_neighbor(0, true));
	zassert_equal(0, library_neighbor(2, false));
}

ZTEST(library_card, test_arquivo_que_sumiu_ganha_marca)
{
	prepare_card(two_tracks);
	insert_and_settle();

	/* Apagado depois da varredura, com o cartão no soquete. */
	remove_file("ODE.TXT");

	zassert_equal(-ENOENT, library_load(1, buf, sizeof(buf), NULL));

	/* Culpa do arquivo: a biblioteca segue, com a faixa marcada. */
	zassert_equal(LIBRARY_READY, library_state());
	zassert_equal(2, library_copy(tracks, ARRAY_SIZE(tracks), NULL));
	zassert_true(tracks[0].valid);
	zassert_false(tracks[1].valid);
	zassert_equal(0, library_neighbor(0, true));
}

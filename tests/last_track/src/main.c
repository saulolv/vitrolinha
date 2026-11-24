/** @file main.c
 * @brief A última faixa lembrada (RF10), sobre o settings de verdade.
 *
 * O settings é o mesmo do firmware: NVS na `storage_partition` da flash
 * simulada. "Reiniciar" aqui é esquecer o que está em RAM
 * (last_track_reset) e chamar last_track_init de novo, que relê a flash; o
 * reinício do processo inteiro, com o arquivo de flash, está no PR que trouxe
 * este módulo.
 *
 * Não há player: o instantâneo é publicado pelo próprio teste, que é o que a
 * thread de gravação lê.
 */
#define _POSIX_C_SOURCE 200809L
#include <vitrolinha/last_track.h>
#include <vitrolinha/library.h>
#include <vitrolinha/snapshot.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

struct file {
	const char *name;
	const char *text;
};

#define CSV "frequency_hz,duration_ms\n1000,100\n"
#define RTTTL "Teste:d=4,o=5,b=120:c"

/* Três faixas de nomes a, b e c. */
static const struct file abc[] = {{"a.csv", CSV}, {"b.csv", CSV}, {"c.csv", CSV}};
/* A mesma b, agora primeira: um arquivo novo mudou a ordem. */
static const struct file moved[] = {{"b.csv", CSV}, {"z.csv", CSV}};
/* Duas faixas de mesmo nome, uma inválida no meio. */
static const struct file twins[] = {
	{"a.rtx", RTTTL}, {"b.csv", CSV}, {"c.rtx", RTTTL}, {"d.rtx", "lixo\n"},
};
/* "Teste" no índice 1, que nas gêmeas é outra faixa. */
static const struct file second[] = {{"a.csv", CSV}, {"b.rtx", RTTTL}};

static char dirs[4][40];
static char path[128];

static void make_dir(int n, const struct file *files, size_t count)
{
	snprintf(dirs[n], sizeof(dirs[n]), "/tmp/vitrolinha-last-%d-XXXXXX", n);
	zassert_not_null(mkdtemp(dirs[n]));
	for (size_t i = 0; i < count; i++) {
		snprintf(path, sizeof(path), "%s/%s", dirs[n], files[i].name);
		FILE *f = fopen(path, "w");

		zassert_not_null(f);
		zassert_true(fputs(files[i].text, f) >= 0);
		zassert_ok(fclose(f));
	}
}

static void remove_dir(int n, const struct file *files, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		snprintf(path, sizeof(path), "%s/%s", dirs[n], files[i].name);
		zassert_ok(unlink(path));
	}
	zassert_ok(rmdir(dirs[n]));
}

static void use_dir(int n, int expected)
{
	zassert_ok(setenv("VITROLINHA_MUSIC_DIR", dirs[n], 1));
	zassert_equal(expected, library_refresh());
}

static void publish(enum player_state state, int16_t track)
{
	const struct player_snapshot snap = {.state = state, .track = track};

	snapshot_publish(&snap);
}

/** @brief Como um boot: esquece a RAM e relê a flash. */
static void reboot(void)
{
	last_track_reset();
	zassert_ok(last_track_init());
}

/** @brief Grava @p track pelo caminho da thread: debounce e parada. */
static void save(int16_t track)
{
	static int64_t now = 100000;

	publish(PLAYER_PAUSED, track);
	zassert_equal(-EAGAIN, last_track_poll(now));
	now += LAST_TRACK_DEBOUNCE_MS;
	zassert_ok(last_track_poll(now));
	now += LAST_TRACK_DEBOUNCE_MS;
}

static void *setup(void)
{
	make_dir(0, abc, ARRAY_SIZE(abc));
	make_dir(1, moved, ARRAY_SIZE(moved));
	make_dir(2, twins, ARRAY_SIZE(twins));
	make_dir(3, second, ARRAY_SIZE(second));
	return NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	const struct player_snapshot idle = SNAPSHOT_IDLE_INITIALIZER;

	snapshot_publish(&idle);
	use_dir(0, 3);
	/* O arquivo de flash sobrevive entre execuções: cada caso parte de
	 * nada gravado.
	 */
	reboot();
	zassert_ok(settings_delete(LAST_TRACK_KEY));
	reboot();
}

static void teardown(void *fixture)
{
	ARG_UNUSED(fixture);
	remove_dir(0, abc, ARRAY_SIZE(abc));
	remove_dir(1, moved, ARRAY_SIZE(moved));
	remove_dir(2, twins, ARRAY_SIZE(twins));
	remove_dir(3, second, ARRAY_SIZE(second));
	zassert_ok(unsetenv("VITROLINHA_MUSIC_DIR"));
}

ZTEST_SUITE(last_track, NULL, setup, before, NULL, teardown);

ZTEST(last_track, test_grava_parada_depois_do_debounce_e_volta_no_boot)
{
	/* Sem faixa, nada a gravar. */
	zassert_equal(-EAGAIN, last_track_poll(0));
	zassert_equal(-EAGAIN, last_track_poll(10000));

	/* Faixa nova: espera o debounce, contado de quando apareceu. */
	publish(PLAYER_PLAYING, 1);
	zassert_equal(-EAGAIN, last_track_poll(1000));
	zassert_equal(-EAGAIN, last_track_poll(1000 + LAST_TRACK_DEBOUNCE_MS - 1));
	/* Tocando, não grava: a escrita travaria as interrupções na placa. */
	zassert_equal(-EBUSY, last_track_poll(1000 + LAST_TRACK_DEBOUNCE_MS));
	publish(PLAYER_PAUSED, 1);
	zassert_ok(last_track_poll(1500 + LAST_TRACK_DEBOUNCE_MS));
	/* Já gravada: as passadas seguintes não escrevem de novo. */
	zassert_ok(last_track_poll(9000));

	/* Antes do init, a restauração espera; depois, devolve a faixa uma vez. */
	last_track_reset();
	zassert_equal(-EAGAIN, last_track_restore());
	zassert_ok(last_track_init());
	zassert_equal(1, last_track_restore());
	zassert_equal(-EALREADY, last_track_restore());
}

ZTEST(last_track, test_nada_gravado_nao_restaura)
{
	zassert_equal(-ENOENT, last_track_restore());
	zassert_equal(-EALREADY, last_track_restore());
}

ZTEST(last_track, test_biblioteca_fora_espera_a_proxima_varredura)
{
	save(2);
	reboot();
	library_reset();
	zassert_equal(-EAGAIN, last_track_restore());
	/* O cartão entra: a mesma chamada, do monitor, agora acha a faixa. */
	use_dir(0, 3);
	zassert_equal(2, last_track_restore());
}

ZTEST(last_track, test_faixa_reconhecida_pelo_nome_quando_a_ordem_muda)
{
	save(1);
	reboot();
	/* A b era a segunda e virou a primeira. */
	use_dir(1, 2);
	zassert_equal(0, last_track_restore());
}

ZTEST(last_track, test_faixa_que_saiu_do_cartao_nao_restaura)
{
	save(2);
	reboot();
	use_dir(1, 2);
	zassert_equal(-ENOENT, last_track_restore());
}

ZTEST(last_track, test_nome_repetido_prefere_o_indice_gravado)
{
	/* Gêmeas em 0 e 2: gravada a 2, volta a 2, e não a primeira. */
	use_dir(2, 4);
	save(2);
	reboot();
	zassert_equal(2, last_track_restore());

	/* Gravada com o índice 1, que nas gêmeas é a b: vale a primeira
	 * "Teste" válida.
	 */
	use_dir(3, 2);
	save(1);
	reboot();
	use_dir(2, 4);
	zassert_equal(0, last_track_restore());
}

ZTEST(last_track, test_faixa_fora_da_lista_nao_grava)
{
	publish(PLAYER_STOPPED, 9);
	zassert_equal(-EAGAIN, last_track_poll(0));
	zassert_equal(-ENOENT, last_track_poll(LAST_TRACK_DEBOUNCE_MS));
}

ZTEST(last_track, test_registro_malformado_e_ignorado)
{
	const char shorter[] = "xy";

	zassert_equal(-EINVAL, settings_runtime_set(LAST_TRACK_KEY, shorter, sizeof(shorter)));
	zassert_equal(-ENOENT, settings_runtime_set("vitrolinha/outra", shorter, sizeof(shorter)));
	zassert_equal(-ENOENT, last_track_restore());
}

/* Por último na ordem alfabética: a thread, uma vez no ar, fica no ar. */
ZTEST(last_track, test_thread_grava_sozinha)
{
	publish(PLAYER_STOPPED, 0);
	last_track_start();
	k_msleep(LAST_TRACK_DEBOUNCE_MS + 3 * LAST_TRACK_POLL_MS);
	reboot();
	zassert_equal(0, last_track_restore());
}

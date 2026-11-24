/** @file main.c
 * @brief Avanço automático (RF09) e faixa lembrada (::CMD_CUE), de ponta a
 *        ponta: CSVs numa pasta -> library -> loader -> player -> captura.
 *
 * Nenhum caso manda ::CMD_NEXT: a faixa seguinte tem de começar sozinha,
 * e é a captura de tons que mostra quando.
 *
 * A biblioteca principal tem quatro faixas, e a 1 é recusada já na
 * varredura (RTTTL sem cabeçalho), com `!`:
 *
 * | Índice | Conteúdo | Eventos |
 * |---|---|---|
 * | 0 | 1000 Hz e 1500 Hz, 100 ms cada | 0, 100, 200 ms (silêncio final) |
 * | 1 | inválida | — |
 * | 2 | 2000 Hz, 100 ms | 0, 100 ms |
 * | 3 | 3000 Hz, 100 ms | 0, 100 ms |
 */
#define _POSIX_C_SOURCE 200809L
#include <vitrolinha/cmd.h>
#include <vitrolinha/cmd_test.h>
#include <vitrolinha/library.h>
#include <vitrolinha/library_test.h>
#include <vitrolinha/loader.h>
#include <vitrolinha/playback_trace.h>
#include <vitrolinha/player.h>
#include <vitrolinha/snapshot.h>
#include <vitrolinha/tone.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zephyr/ztest.h>

static char full_dir[] = "/tmp/vitrolinha-advance-XXXXXX";
static char single_dir[] = "/tmp/vitrolinha-single-XXXXXX";
static char broken_dir[] = "/tmp/vitrolinha-broken-XXXXXX";
static char path[128];

static const char *const full_files[][2] = {
	{"0.csv", "frequency_hz,duration_ms\n1000,100\n1500,100\n"},
	{"1.rtx", "sem cabecalho\n"},
	{"2.csv", "frequency_hz,duration_ms\n2000,100\n"},
	{"3.csv", "frequency_hz,duration_ms\n3000,100\n"},
};

/* Uma só faixa válida: a vizinha dela é ela mesma. */
static const char *const single_files[][2] = {
	{"0.csv", "frequency_hz,duration_ms\n1000,100\n"},
	{"1.rtx", "sem cabecalho\n"},
};

/* A seguinte passa na varredura e só o parser a recusa: duração zero. */
static const char *const broken_files[][2] = {
	{"0.csv", "frequency_hz,duration_ms\n1000,100\n"},
	{"1.csv", "frequency_hz,duration_ms\n1000,0\n"},
};

static uint64_t clock_us(void)
{
	return k_cyc_to_us_floor64(k_cycle_get_64());
}

static void write_files(const char *dir, const char *const files[][2], size_t count)
{
	for (size_t i = 0; i < count; i++) {
		snprintf(path, sizeof(path), "%s/%s", dir, files[i][0]);
		FILE *file = fopen(path, "w");

		zassert_not_null(file);
		zassert_true(fputs(files[i][1], file) >= 0);
		zassert_ok(fclose(file));
	}
}

static void remove_files(const char *dir, const char *const files[][2], size_t count)
{
	for (size_t i = 0; i < count; i++) {
		snprintf(path, sizeof(path), "%s/%s", dir, files[i][0]);
		zassert_ok(unlink(path));
	}
	zassert_ok(rmdir(dir));
}

/** @brief Aponta a biblioteca para uma pasta e a varre. */
static void use_library(const char *dir, int expected)
{
	zassert_ok(setenv("VITROLINHA_MUSIC_DIR", dir, 1));
	zassert_equal(expected, library_refresh());
}

static void *setup(void)
{
	zassert_not_null(mkdtemp(full_dir));
	zassert_not_null(mkdtemp(single_dir));
	write_files(full_dir, full_files, ARRAY_SIZE(full_files));
	write_files(single_dir, single_files, ARRAY_SIZE(single_files));
	zassert_not_null(mkdtemp(broken_dir));
	write_files(broken_dir, broken_files, ARRAY_SIZE(broken_files));
	use_library(full_dir, 4);
	zassert_ok(loader_init());
	zassert_ok(tone_init());
	zassert_ok(player_init());
	return NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	cmd_flush();
	use_library(full_dir, 4);
	zassert_ok(playback_trace_begin(clock_us()));
}

static void after(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_ok(cmd_post(CMD_STOP, TRACK_NONE));
	k_msleep(5);
	(void)playback_trace_end(clock_us());
}

static void teardown(void *fixture)
{
	ARG_UNUSED(fixture);
	remove_files(full_dir, full_files, ARRAY_SIZE(full_files));
	remove_files(single_dir, single_files, ARRAY_SIZE(single_files));
	remove_files(broken_dir, broken_files, ARRAY_SIZE(broken_files));
	zassert_ok(unsetenv("VITROLINHA_MUSIC_DIR"));
}

static struct player_snapshot await_state(enum player_state wanted, int track)
{
	struct player_snapshot snap;

	for (int i = 0; i < 2000; i++) {
		snapshot_read(&snap);
		if (snap.state == wanted && snap.track == track) {
			return snap;
		}
		k_msleep(1);
	}
	zassert_true(false, "esperado estado %d, faixa %d; obtido %u, %d", wanted, track,
		     snap.state, snap.track);
	return snap;
}

/** @brief Fica parado na faixa, no fim, sem avançar: espera e confere de novo. */
static void expect_stays_stopped_at_end(int track)
{
	struct player_snapshot snap = await_state(PLAYER_STOPPED, track);

	zassert_equal(snap.total_us, snap.base_us, "parou antes do fim");
	k_msleep(300);
	snapshot_read(&snap);
	zassert_equal(PLAYER_STOPPED, snap.state);
	zassert_equal(track, snap.track, "avancou para %d", snap.track);
	zassert_equal(snap.total_us, snap.base_us);
}

/** @brief Posição da primeira troca para @p freq a partir de @p from; -1 se nenhuma. */
static int find_tone(size_t from, uint32_t freq, struct playback_trace_entry *out)
{
	struct playback_trace_entry entry;

	for (size_t i = from; playback_trace_read(i, &entry) == 1; i++) {
		if (entry.freq_hz == freq) {
			*out = entry;
			return (int)i;
		}
	}
	return -1;
}

/* O nome começa por 0: os casos rodam em ordem alfabética, e este precisa do
 * player recém-nascido, ainda sem faixa nenhuma.
 */
ZTEST(auto_advance, test_0_faixa_lembrada_fica_selecionada_sem_tocar)
{
	struct player_snapshot snap;

	/* A faixa do boot: o player fica com ela, parado e mudo. */
	zassert_ok(cmd_post(CMD_CUE, 2));
	snap = await_state(PLAYER_STOPPED, 2);
	zassert_equal(0, snap.total_us);
	zassert_equal(0, snap.load_error);

	/* Uma segunda faixa lembrada não troca a primeira. */
	zassert_ok(cmd_post(CMD_CUE, 0));
	k_msleep(5);
	snapshot_read(&snap);
	zassert_equal(2, snap.track);

	/* Nada tocou até aqui. */
	struct playback_trace_entry entry;

	zassert_ok(playback_trace_end(clock_us()));
	/* Só o registro de fechamento da captura, que é silêncio. */
	zassert_equal(1, playback_trace_read(0, &entry));
	zassert_equal(0, entry.freq_hz, "a faixa lembrada nao pode soar");
	zassert_equal(0, playback_trace_read(1, &entry), "a faixa lembrada nao pode soar");
	zassert_ok(playback_trace_begin(clock_us()));

	/* Tocar toca a lembrada. */
	zassert_ok(cmd_post(CMD_PLAY_PAUSE, TRACK_NONE));
	(void)await_state(PLAYER_PLAYING, 2);

	/* E, já com faixa, outra lembrança não muda nada. */
	zassert_ok(cmd_post(CMD_CUE, 0));
	k_msleep(1);
	snapshot_read(&snap);
	zassert_equal(2, snap.track);
}

ZTEST(auto_advance, test_fim_da_faixa_carrega_a_seguinte_pulando_a_invalida)
{
	struct playback_trace_entry silence, first;

	zassert_ok(cmd_post(CMD_SELECT, 0));
	(void)await_state(PLAYER_PLAYING, 0);
	/* Sem comando nenhum daqui em diante: 0, depois 2 (a 1 tem `!`), e 3. */
	(void)await_state(PLAYER_PLAYING, 2);
	(void)await_state(PLAYER_PLAYING, 3);
	expect_stays_stopped_at_end(3);
	zassert_ok(playback_trace_end(clock_us()));

	/* O silêncio final da faixa 0 é a troca seguinte ao 1500 Hz. */
	int last = find_tone(0, 1500, &silence);

	zassert_true(last > 0);
	zassert_equal(1, playback_trace_read(last + 1, &silence));
	zassert_equal(0, silence.freq_hz);

	/* A seguinte começa logo depois: carregar 30 bytes de RAM, no
	 * simulador, não custa tempo de relógio.
	 */
	int next = find_tone(last, 2000, &first);

	zassert_true(next > last);
	zassert_true(first.elapsed_us - silence.elapsed_us < 20000U,
		     "a seguinte comecou %llu us depois do fim",
		     first.elapsed_us - silence.elapsed_us);

	/* A faixa 1, recusada, não soou: nenhuma troca sai de outra faixa
	 * entre o fim da 0 e o começo da 2.
	 */
	struct playback_trace_entry entry;

	for (int i = last + 2; i < next; i++) {
		zassert_equal(1, playback_trace_read(i, &entry));
		zassert_equal(0, entry.freq_hz, "troca %d soou %u Hz", i, entry.freq_hz);
	}

	/* A 3 é a última: depois dela, nada volta à 0. */
	zassert_true(find_tone(next, 3000, &entry) > next);
	zassert_equal(-1, find_tone(next, 1000, &entry), "deu a volta na lista");
}

ZTEST(auto_advance, test_ultima_faixa_nao_da_a_volta)
{
	zassert_ok(cmd_post(CMD_SELECT, 3));
	(void)await_state(PLAYER_PLAYING, 3);
	expect_stays_stopped_at_end(3);
}

ZTEST(auto_advance, test_cartao_fora_termina_a_faixa_e_para)
{
	zassert_ok(cmd_post(CMD_SELECT, 0));
	(void)await_state(PLAYER_PLAYING, 0);
	/* O cartão sai no meio: a faixa em RAM termina, e não há seguinte. */
	library_reset();
	expect_stays_stopped_at_end(0);
}

ZTEST(auto_advance, test_faixa_unica_toca_uma_vez_e_para)
{
	use_library(single_dir, 2);
	zassert_ok(cmd_post(CMD_SELECT, 0));
	(void)await_state(PLAYER_PLAYING, 0);
	/* A vizinha da única faixa válida é ela mesma: não repete. */
	expect_stays_stopped_at_end(0);
}

ZTEST(auto_advance, test_seguinte_recusada_pelo_parser_para_com_o_erro)
{
	use_library(broken_dir, 2);
	zassert_ok(cmd_post(CMD_SELECT, 0));
	(void)await_state(PLAYER_PLAYING, 0);
	/* A 1 passou na varredura; recusada na carga, o player para nela
	 * com o motivo, como num "seguinte" do usuário, e não insiste.
	 */
	struct player_snapshot snap = await_state(PLAYER_STOPPED, 1);

	zassert_equal(-EINVAL, snap.load_error);
	k_msleep(300);
	snapshot_read(&snap);
	zassert_equal(PLAYER_STOPPED, snap.state);
	zassert_equal(1, snap.track);
	/* E a faixa ganhou o `!`: não é mais vizinha de ninguém. */
	zassert_equal(0, library_neighbor(0, true));
}

ZTEST_SUITE(auto_advance, NULL, setup, before, after, teardown);

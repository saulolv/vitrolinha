/** @file main.c
 * @brief O primeiro comando do player, antes de haver faixa selecionada.
 *
 * Seguinte, anterior e tocar partem de "nenhuma faixa" de modos diferentes,
 * e com a biblioteca vazia nenhum deles tem para onde ir. Cada cenário do
 * testcase.yaml é um processo, porque o player só sai de "nenhuma faixa" uma
 * vez. A biblioteca é a de mentira, com duas faixas, varrida ou não pela
 * `library`; o que se confere é só a faixa escolhida, publicada antes da
 * carga.
 */
#include <vitrolinha/cmd.h>
#include <vitrolinha/library.h>
#include <vitrolinha/loader.h>
#include <vitrolinha/player.h>
#include <vitrolinha/snapshot.h>
#include <vitrolinha/tone.h>

#include <zephyr/ztest.h>

ZTEST(player_first_command, test_primeiro_comando_sem_faixa)
{
	struct player_snapshot snap;

	zassert_ok(loader_init());
	zassert_ok(tone_init());
	zassert_ok(player_init());
	if (!IS_ENABLED(CONFIG_TEST_CARD)) {
		/* Nada tem para onde ir. */
		zassert_ok(cmd_post(CMD_NEXT, TRACK_NONE));
		zassert_ok(cmd_post(CMD_PREV, TRACK_NONE));
		zassert_ok(cmd_post(CMD_PLAY_PAUSE, TRACK_NONE));
		zassert_ok(cmd_post(CMD_SELECT, 0));
	} else {
		zassert_equal(2, library_refresh());
		if (IS_ENABLED(CONFIG_TEST_REJECT_ALL)) {
			/* Cartão pronto, nenhuma faixa tocável. */
			zassert_ok(library_reject(0, library_generation()));
			zassert_ok(library_reject(1, library_generation()));
		}
		zassert_ok(cmd_post(CONFIG_TEST_FIRST_COMMAND, TRACK_NONE));
	}
	k_msleep(10);
	snapshot_read(&snap);
	zassert_equal(PLAYER_STOPPED, snap.state);
	zassert_equal(CONFIG_TEST_EXPECTED_TRACK, snap.track);
}

ZTEST_SUITE(player_first_command, NULL, NULL, NULL, NULL, NULL);

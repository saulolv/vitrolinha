/**
 * @file main.c
 * @brief Testes de src/buttons.c: botão apertado vira comando na fila.
 *
 * O evento entra pelo mesmo caminho do gpio-keys, `input_report_key`, e o
 * que se confere é o que chega à fila do player.
 */

#include <vitrolinha/cmd.h>
#include <vitrolinha/cmd_test.h>
#include <vitrolinha/display.h>
#include <vitrolinha/track.h>

#include <errno.h>

#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/** @brief Aperta e solta um botão. */
static void click(uint16_t code)
{
	zassert_ok(input_report_key(NULL, code, 1, true, K_FOREVER));
	zassert_ok(input_report_key(NULL, code, 0, true, K_FOREVER));
}

/** @brief Retira o próximo comando e confere o tipo. */
static void expect_cmd(enum cmd_kind kind)
{
	struct cmd got;

	zassert_ok(cmd_get(&got, K_NO_WAIT), "fila vazia, esperava o comando %d", kind);
	zassert_equal((uint8_t)kind, got.kind, "veio o comando %d, esperava %d", got.kind, kind);
	zassert_equal(TRACK_NONE, got.track);
}

static void expect_empty(void)
{
	struct cmd got;

	zassert_equal(-ENOMSG, cmd_get(&got, K_NO_WAIT), "sobrou comando na fila");
}

/** @brief Enche a fila deixando @p free posições livres. */
static void fill_queue(size_t free)
{
	for (size_t i = 0U; i < (CMD_QUEUE_DEPTH - free); i++) {
		zassert_ok(cmd_post(CMD_NEXT, TRACK_NONE));
	}
}

static void buttons_before(void *fixture)
{
	ARG_UNUSED(fixture);
	cmd_flush();
}

ZTEST_SUITE(buttons, NULL, NULL, buttons_before, NULL, NULL);

ZTEST(buttons, test_botao_2_pausa_e_retoma)
{
	/* Um comando por aperto: o player alterna entre pausar e retomar. */
	click(INPUT_BTN_1);
	click(INPUT_BTN_1);

	expect_cmd(CMD_PLAY_PAUSE);
	expect_cmd(CMD_PLAY_PAUSE);
	expect_empty();
}

ZTEST(buttons, test_botao_de_alternar_tela_nao_publica_comando)
{
	/* É da interface, não do transporte: quem o atende é o display_ui.c. */
	click(DISPLAY_TOGGLE_BUTTON);

	expect_empty();
}

ZTEST(buttons, test_botoes_1_e_3_trocam_de_faixa)
{
	/* Um comando por aperto; a volta nas pontas é do player. */
	click(INPUT_BTN_0);
	click(INPUT_BTN_2);
	click(INPUT_BTN_2);

	expect_cmd(CMD_PREV);
	expect_cmd(CMD_NEXT);
	expect_cmd(CMD_NEXT);
	expect_empty();
}

ZTEST(buttons, test_soltar_nao_publica)
{
	zassert_ok(input_report_key(NULL, INPUT_BTN_1, 0, true, K_FOREVER));

	expect_empty();
}

ZTEST(buttons, test_o_que_nao_e_botao_e_ignorado)
{
	/* O clique do encoder é do LVGL, não do transporte. */
	click(INPUT_KEY_ENTER);
	/* Código logo depois dos quatro botões. */
	click(INPUT_BTN_0 + 4U);
	/* O giro do encoder: outro tipo de evento. */
	zassert_ok(input_report_rel(NULL, INPUT_REL_WHEEL, 1, true, K_FOREVER));

	expect_empty();
}

ZTEST(buttons, test_codigo_abaixo_dos_botoes_e_ignorado)
{
	click(INPUT_BTN_0 - 1U);

	expect_empty();
}

ZTEST(buttons, test_fila_cheia_descarta_a_pausa)
{
	fill_queue(0U);

	click(INPUT_BTN_1);

	for (size_t i = 0U; i < CMD_QUEUE_DEPTH; i++) {
		expect_cmd(CMD_NEXT);
	}
	expect_empty();
}

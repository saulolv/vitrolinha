/**
 * @file buttons.c
 * @brief Botões de transporte: cada aperto vira um comando na fila do player.
 *
 * É a metade `input` da arquitetura: escuta o subsistema `input` do Zephyr e
 * publica em @ref cmd.h, sem nunca falar com o player diretamente. O encoder
 * não passa por aqui — o clique dele é `INPUT_KEY_ENTER`, do LVGL, e os
 * botões são `INPUT_BTN_0..3`.
 *
 * A ordem física, da esquerda para a direita, é a numérica: BTN_0 é o botão
 * 1 da placa, e assim por diante — identificada na bancada (issue #27), já
 * que o P2 não tem serigrafia. A tabela segue a especificação: anterior,
 * tocar/pausar, seguinte e alternar telas. Todo aperto sai no console com o
 * código, para conferir.
 *
 * O botão ::DISPLAY_TOGGLE_BUTTON também passa por aqui, mas não é
 * transporte: alterna as telas, e quem o atende é o `display.c`. Este
 * módulo só o registra no console.
 *
 * Não há botão de reiniciar: no fim da faixa, tocar/pausar já recomeça do
 * início, e clicar de novo a faixa na biblioteca também.
 */

#include <vitrolinha/cmd.h>
#include <vitrolinha/display.h>

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(buttons, LOG_LEVEL_INF);

/** @brief Botão que não publica comando: o de alternar telas. */
#define BUTTON_NO_CMD CMD_KIND_COUNT

/** @brief Comando de cada botão, indexado por `código - INPUT_BTN_0`. */
static const enum cmd_kind button_map[] = {
	CMD_PREV,       /* INPUT_BTN_0, botão 1: anterior */
	CMD_PLAY_PAUSE, /* INPUT_BTN_1, botão 2: tocar/pausar */
	CMD_NEXT,       /* INPUT_BTN_2, botão 3: seguinte */
	BUTTON_NO_CMD,  /* INPUT_BTN_3, botão 4: alterna as telas, no display.c */
};

/* A linha sem comando da tabela tem de ser a do botão que o display atende;
 * mudar um sem o outro deixaria um botão mudo, ou fazendo duas coisas.
 */
BUILD_ASSERT(DISPLAY_TOGGLE_BUTTON == INPUT_BTN_3,
	     "button_map: mova BUTTON_NO_CMD para a linha de DISPLAY_TOGGLE_BUTTON");

/**
 * @brief Publica o comando de um botão apertado.
 *
 * Nunca bloqueia: @ref cmd_post descarta com fila cheia.
 */
static void button_pressed(uint16_t code)
{
	size_t index = (size_t)(code - INPUT_BTN_0);
	enum cmd_kind kind = button_map[index];
	int err;

	LOG_INF("Botao %zu apertado", index);

	if (kind == BUTTON_NO_CMD) {
		return;
	}

	err = cmd_post(kind, TRACK_NONE);
	if (err != 0) {
		LOG_WRN("Comando do botao %zu descartado: %d", index, err);
	}
}

/**
 * @brief Recebe todo evento de entrada e fica só com os apertos de botão.
 *
 * Soltar o botão não faz nada: o comando sai na borda de descida, que é a
 * que o dedo sente como "apertei".
 */
static void buttons_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if ((evt->type != INPUT_EV_KEY) || (evt->value == 0)) {
		return;
	}

	if ((evt->code < INPUT_BTN_0) || (evt->code >= (INPUT_BTN_0 + ARRAY_SIZE(button_map)))) {
		return;
	}

	button_pressed(evt->code);
}

INPUT_CALLBACK_DEFINE(NULL, buttons_cb, NULL);

/**
 * @file buttons.c
 * @brief Botões de transporte: cada aperto vira um comando na fila do player.
 *
 * É a metade `input` da arquitetura: escuta o subsistema `input` do Zephyr e
 * publica em @ref cmd.h, sem nunca falar com o player diretamente. O encoder
 * não passa por aqui — o clique dele é `INPUT_KEY_ENTER`, do LVGL, e os
 * botões são `INPUT_BTN_0..3`.
 *
 * O mapeamento é por código, e não por posição física: o P2 não tem
 * serigrafia e a ordem física não segue a numérica (issue #27). Por isso todo
 * aperto sai no console com o código, para a bancada descobrir qual botão é
 * qual e ajustar ::button_map.
 *
 * Reiniciar não é comando próprio: é ::CMD_STOP seguido de
 * ::CMD_PLAY_PAUSE, que o player já trata como "tocar de novo do começo",
 * a partir da cópia em RAM e sem ler o cartão.
 */

#include <vitrolinha/cmd.h>

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(buttons, LOG_LEVEL_INF);

/** @brief Marca o fim da sequência de um botão que publica um comando só. */
#define BUTTON_NO_CMD CMD_KIND_COUNT

/**
 * @brief Comandos de cada botão, em ordem, indexados por `código - INPUT_BTN_0`.
 *
 * - tocar/pausar: pausa; apertado de novo, retoma de onde parou;
 * - reiniciar: volta a faixa ao começo e toca;
 * - anterior e seguinte: trocam de faixa, dando a volta nas pontas.
 */
static const enum cmd_kind button_map[][2] = {
	{CMD_PLAY_PAUSE, BUTTON_NO_CMD}, /* INPUT_BTN_0: tocar/pausar */
	{CMD_STOP, CMD_PLAY_PAUSE},      /* INPUT_BTN_1: reiniciar */
	{CMD_PREV, BUTTON_NO_CMD},       /* INPUT_BTN_2: anterior */
	{CMD_NEXT, BUTTON_NO_CMD},       /* INPUT_BTN_3: seguinte */
};

/**
 * @brief Publica os comandos de um botão apertado.
 *
 * Nunca bloqueia: @ref cmd_post descarta com fila cheia. No reinício, se o
 * ::CMD_STOP não coube, o ::CMD_PLAY_PAUSE nem é tentado — sozinho ele
 * pausaria em vez de reiniciar.
 */
static void button_pressed(uint16_t code)
{
	size_t index = (size_t)(code - INPUT_BTN_0);
	const enum cmd_kind *cmds = button_map[index];
	int err;

	LOG_INF("Botao %zu apertado", index);

	err = cmd_post(cmds[0], TRACK_NONE);
	if ((err == 0) && (cmds[1] != BUTTON_NO_CMD)) {
		err = cmd_post(cmds[1], TRACK_NONE);
	}

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

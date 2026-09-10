/**
 * @file sim_encoder.c
 * @brief Rotação do encoder no alvo de simulação, a partir das setas.
 *
 * Só entra na compilação para `native_sim`.
 *
 * Na placa, girar o encoder produz quadratura nas linhas A e B, e o driver
 * `gpio-qdec` a converte em ::INPUT_REL_WHEEL. No simulador as entradas vêm
 * do teclado do host, e duas teclas não produzem quadratura limpa — apertar
 * uma seta gera uma borda só, não o par defasado que o decodificador espera.
 *
 * Então aqui as setas são teclas comuns, e este módulo faz a última etapa da
 * tradução: recebe ::INPUT_KEY_LEFT e ::INPUT_KEY_RIGHT e emite o mesmo
 * ::INPUT_REL_WHEEL que o `gpio-qdec` emitiria. O dispositivo de entrada do
 * LVGL não distingue os dois casos — é o mesmo código de evento chegando
 * pelo mesmo barramento.
 *
 * O que fica de fora da simulação, e continua sendo verificação de bancada:
 * se o encoder físico lê limpo, se precisa de pull-up externo, e quantos
 * detentes valem um passo (issue #27).
 */

#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sim_encoder, LOG_LEVEL_INF);

/** @brief Um detente por tecla, na direção correspondente. */
#define SIM_ENCODER_STEP 1

/**
 * @brief Converte tecla de seta em passo de encoder.
 *
 * Reage apenas ao pressionar, não ao soltar: uma tecla mantida gera repetição
 * pelo próprio teclado, o que dá um giro contínuo razoável.
 *
 * @param evt       Evento vindo do subsistema `input`.
 * @param user_data Não usado.
 */
static void sim_encoder_on_key(struct input_event *evt, void *user_data)
{
	int32_t step;

	ARG_UNUSED(user_data);

	if (evt->type != INPUT_EV_KEY || evt->value == 0) {
		return;
	}

	switch (evt->code) {
	case INPUT_KEY_LEFT:
		step = -SIM_ENCODER_STEP;
		break;
	case INPUT_KEY_RIGHT:
		step = SIM_ENCODER_STEP;
		break;
	default:
		return;
	}

	/* Sem espera: isto roda no contexto de quem publicou o evento de
	 * tecla, e bloquear ali seguraria todo o subsistema de entrada.
	 */
	(void)input_report_rel(NULL, INPUT_REL_WHEEL, step, true, K_NO_WAIT);
}

/* NULL como dispositivo: escuta todos os produtores de entrada, e filtra por
 * código. É a mesma escolha do nó do LVGL no app.overlay.
 */
INPUT_CALLBACK_DEFINE(NULL, sim_encoder_on_key, NULL);

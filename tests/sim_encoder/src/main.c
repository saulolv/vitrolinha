/**
 * @file main.c
 * @brief Testes de `src/sim_encoder.c`.
 *
 * O módulo traduz setas do teclado em passos de encoder no alvo simulado.
 * Ele existe só para a bancada, mas é testado como o resto: se a tradução
 * estiver errada, a navegação do simulador diverge da navegação da placa, e
 * toda a interface passa a ser desenvolvida contra um comportamento que a
 * placa não tem.
 *
 * O teste ouve o barramento de entrada pelo mesmo mecanismo que o LVGL usa,
 * e verifica o que chega lá — não o que o módulo faz por dentro.
 */

#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/** @brief Último evento de rotação observado, e quantos houve. */
static int32_t last_wheel;
static int wheel_count;

/**
 * @brief Espia o barramento de entrada, como faz o dispositivo do LVGL.
 *
 * @param evt       Evento publicado.
 * @param user_data Não usado.
 */
static void spy(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type == INPUT_EV_REL && evt->code == INPUT_REL_WHEEL) {
		last_wheel = evt->value;
		wheel_count++;
	}
}

INPUT_CALLBACK_DEFINE(NULL, spy, NULL);

static void sim_encoder_before(void *fixture)
{
	ARG_UNUSED(fixture);
	last_wheel = 0;
	wheel_count = 0;
}

ZTEST_SUITE(sim_encoder, NULL, NULL, sim_encoder_before, NULL, NULL);

/**
 * @brief Publica um evento de tecla, como o `input_gpio_keys` faria.
 *
 * @param code    Código da tecla.
 * @param pressed 1 para apertar, 0 para soltar.
 */
static void press(uint16_t code, int32_t pressed)
{
	zassert_ok(input_report_key(NULL, code, pressed, true, K_NO_WAIT));
}

ZTEST(sim_encoder, test_seta_direita_gira_no_sentido_horario)
{
	press(INPUT_KEY_RIGHT, 1);

	zassert_equal(1, wheel_count, "deveria ter saido exatamente um passo");
	zassert_equal(1, last_wheel, "seta direita gira no sentido positivo");
}

ZTEST(sim_encoder, test_seta_esquerda_gira_no_sentido_oposto)
{
	press(INPUT_KEY_LEFT, 1);

	zassert_equal(1, wheel_count);
	zassert_equal(-1, last_wheel, "seta esquerda gira no sentido negativo");
}

ZTEST(sim_encoder, test_soltar_a_tecla_nao_gira)
{
	press(INPUT_KEY_RIGHT, 1);
	press(INPUT_KEY_RIGHT, 0);

	/* Um aperto é um detente. Se soltar também contasse, cada toque
	 * andaria duas posições na lista.
	 */
	zassert_equal(1, wheel_count, "soltar nao pode gerar um segundo passo");
}

ZTEST(sim_encoder, test_outras_teclas_sao_ignoradas)
{
	press(INPUT_KEY_ENTER, 1);
	press(INPUT_BTN_0, 1);
	press(INPUT_KEY_UP, 1);

	/* O clique do encoder e os botões de transporte seguem por outro
	 * caminho: quem os consome é o dispositivo de entrada do LVGL e a
	 * fila de comandos, não a rotação.
	 */
	zassert_equal(0, wheel_count);
}

ZTEST(sim_encoder, test_eventos_que_nao_sao_de_tecla_sao_ignorados)
{
	zassert_ok(input_report_rel(NULL, INPUT_REL_X, 5, true, K_NO_WAIT));

	zassert_equal(0, wheel_count, "so evento de tecla vira rotacao");
}

ZTEST(sim_encoder, test_apertos_sucessivos_acumulam_passos)
{
	press(INPUT_KEY_RIGHT, 1);
	press(INPUT_KEY_RIGHT, 1);
	press(INPUT_KEY_LEFT, 1);

	zassert_equal(3, wheel_count);
	zassert_equal(-1, last_wheel, "o ultimo passo foi para tras");
}

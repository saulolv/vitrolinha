/**
 * @file main.c
 * @brief Ponto de entrada do Vitrolinha: põe os módulos de pé, na ordem.
 *
 * 1. O LED de sinal de vida.
 * 2. O cartão e a biblioteca: o monitor do cartão, a primeira varredura, e o
 *    observador que varre de novo a cada troca de cartão.
 * 3. A interface, que mostra a biblioteca.
 * 4. A reprodução: loader, saída de tom e player.
 * 5. As extensões da E7: a última faixa lembrada (RF10) e os LEDs de estado
 *    (RF11). O avanço automático (RF09) é do próprio player.
 *
 * Depois disso o `main` só pisca o LED: o trabalho está nas threads de cada
 * módulo.
 *
 * Compila para os dois alvos: a ZBook P2 e o simulador. O que difere entre
 * eles é devicetree e configuração, não código.
 */

#include <vitrolinha/card.h>
#include <vitrolinha/cmd.h>
#include <vitrolinha/display.h>
#include <vitrolinha/library.h>

#if defined(CONFIG_VITROLINHA_LAST_TRACK)
#include <vitrolinha/last_track.h>
#endif

#if defined(CONFIG_VITROLINHA_STATUS_LED)
#include <vitrolinha/status_led.h>
#endif

#include <app_version.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/*
 * O player só entra no build onde há saída para ele: na placa, o buzzer do
 * tone_pwm; no simulador, a captura em memória do tone_trace. Quem decide é
 * `CONFIG_VITROLINHA_PLAYER`, a mesma opção pela qual src/CMakeLists.txt
 * compila player e loader; sem ela, chamar player_init() não linkaria.
 *
 * A captura é aberta aqui onde src/CMakeLists.txt compila o playback_trace.
 */
#if defined(CONFIG_BOARD_NATIVE_SIM) && defined(CONFIG_EXTERNAL_LIBC)
#define VITROLINHA_HAS_TRACE 1
#include <vitrolinha/playback_trace.h>
#endif

#if defined(CONFIG_VITROLINHA_PLAYER)
#define VITROLINHA_HAS_PLAYER 1
#include <vitrolinha/loader.h>
#include <vitrolinha/player.h>
#include <vitrolinha/tone.h>
#endif

LOG_MODULE_REGISTER(vitrolinha, LOG_LEVEL_INF);

/*
 * Verificações de compilação da placa física. No alvo de simulação não há
 * encoder, display SSD1306 nem cartão para conferir — os periféricos
 * emulados são outros, e o overlay que os descreve é outro.
 */
#if defined(CONFIG_BOARD_ZBOOK)
#include "board_zbook.h"
#endif

/** @brief Período de meio piscar do LED de sinal de vida. */
#define HEARTBEAT_PERIOD K_MSEC(500)

/**
 * @brief LED de sinal de vida.
 *
 * Um dos quatro LEDs de GPIO puro. Um deles vai virar o pino de
 * instrumentação das medições de tempo (issue #23), já que a placa não tem
 * pino de header confirmado — daí valer a pena que o sinal de vida seja um
 * ponto único no código, fácil de mover.
 */
static const struct gpio_dt_spec heartbeat = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/**
 * @brief Prepara o LED de sinal de vida.
 *
 * @retval 0        LED pronto.
 * @retval -ENODEV  O controlador de GPIO não inicializou.
 * @retval outro    Erro devolvido por `gpio_pin_configure_dt`.
 */
static int heartbeat_init(void)
{
	int err;

	if (!gpio_is_ready_dt(&heartbeat)) {
		LOG_ERR("GPIO do LED de sinal de vida nao esta pronto");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&heartbeat, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		LOG_ERR("Nao consegui configurar o LED: %d", err);
		return err;
	}

	return 0;
}

/**
 * @brief Devolve a última faixa lembrada, uma vez por boot (RF10).
 *
 * Vale na primeira biblioteca pronta: no boot, se o cartão já leu, ou na
 * primeira inserção depois dele. O player recebe a faixa por ::CMD_CUE, que
 * ele só aceita enquanto não tem faixa nenhuma, e a tela põe o destaque
 * nela. Nada toca: tocar/pausar é que a toca.
 */
static void restore_last_track(void)
{
#if defined(CONFIG_VITROLINHA_LAST_TRACK)
	int track = last_track_restore();

	if (track >= 0) {
		LOG_INF("Ultima faixa restaurada: %d", track);
		(void)cmd_post(CMD_CUE, (int16_t)track);
		display_cue(track);
	}
#endif
}

/**
 * @brief Leva toda mudança de estado do cartão à biblioteca.
 *
 * Roda na thread do monitor do cartão, e varre ali mesmo: é a thread que já
 * bloqueia por natureza, e a única de prioridade baixa o bastante para isso
 * não atrasar ninguém (ADR 0010). A interface lê o resultado em
 * @ref library_state a cada quadro.
 *
 * @param state O estado novo do cartão.
 */
static void card_changed(enum card_state state)
{
	/* A transição o próprio módulo do cartão já registra no log. */
	ARG_UNUSED(state);

	(void)library_refresh();
	restore_last_track();
}

/**
 * @brief Monta o cartão, põe o monitor de presença de pé e varre a
 *        biblioteca.
 *
 * Ausência de cartão não é falha de inicialização: a placa continua ligando,
 * e o monitor passa a avisar se um cartão aparecer.
 */
static void card_start(void)
{
	int err;

	err = card_init();
	if (err != 0) {
		LOG_ERR("Monitor do cartao nao subiu: %d", err);
	}

	/* Observar **depois** de inicializar, e varrer em seguida. O contrato
	 * do card sugere a ordem inversa, para receber a transição inicial,
	 * mas aqui ela faria a biblioteca ser varrida duas vezes no boot: uma
	 * pelo observador, dentro de card_init, e outra pela varredura
	 * explícita — que não pode faltar, porque no simulador o cartão nunca
	 * transita e quem responde é a implementação de mentira.
	 *
	 * A ordem não abre janela: o monitor dorme 250 ms antes da primeira
	 * sondagem, e qualquer transição depois de card_observe chega ao
	 * observador. No pior caso, uma varredura a mais.
	 */
	card_observe(card_changed);
	(void)library_refresh();
}

#if defined(VITROLINHA_HAS_PLAYER)
/**
 * @brief Põe loader, saída e player de pé, na ordem que o player.h pede.
 *
 * Na placa a saída é o buzzer. No simulador é a captura do
 * @ref playback_trace.h, aberta aqui e nunca fechada: comporta
 * PLAYBACK_TRACE_CAPACITY trocas de tom por sessão. Esgotada, o player
 * recebe -ENOSPC da saída e para a faixa — o limite é da bancada, não do
 * player.
 *
 * Falha aqui não derruba o `main`: a tela continua listando a biblioteca.
 *
 * O player lê a biblioteca a cada comando (library.h), e por isso não
 * recebe a contagem: um cartão inserido depois do boot já vale para ele.
 */
static void playback_start(void)
{
	int err;

	err = loader_init();
	if (err != 0) {
		LOG_ERR("Loader nao subiu: %d", err);
		return;
	}

	err = tone_init();
	if (err != 0) {
		LOG_ERR("Saida de tom nao subiu: %d", err);
		return;
	}

#if defined(VITROLINHA_HAS_TRACE)
	/* O mesmo relógio que o tone_trace usa para cada registro. */
	err = playback_trace_begin(k_cyc_to_us_floor64(k_cycle_get_64()));
	if (err != 0) {
		LOG_ERR("Captura nao abriu: %d", err);
		return;
	}
#endif

	err = player_init();
	if (err != 0) {
		LOG_ERR("Player nao subiu: %d", err);
	}
}
#endif

int main(void)
{
	int err;

	LOG_INF("Vitrolinha %s em " CONFIG_BOARD_TARGET, APP_VERSION_STRING);

	err = heartbeat_init();
	if (err != 0) {
		return err;
	}

	card_start();

	/* Depois do cartão: a lista da tela sai da varredura do volume montado. */
	err = display_init();
	if (err != 0) {
		return err;
	}

#if defined(VITROLINHA_HAS_PLAYER)
	playback_start();
#endif

#if defined(CONFIG_VITROLINHA_LAST_TRACK)
	/* Depois do player, que é quem recebe a faixa lembrada. */
	err = last_track_init();
	if (err != 0) {
		LOG_ERR("Settings nao subiu: %d", err);
	}
	restore_last_track();
	last_track_start();
#endif

#if defined(CONFIG_VITROLINHA_STATUS_LED)
	err = status_led_init();
	if (err != 0) {
		LOG_ERR("LEDs de estado nao subiram: %d", err);
	}
#endif

	LOG_INF("Vitrolinha no ar. LED de sinal de vida piscando.");

	while (true) {
		(void)gpio_pin_toggle_dt(&heartbeat);
		k_sleep(HEARTBEAT_PERIOD);
	}

	return 0;
}

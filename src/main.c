/**
 * @file main.c
 * @brief Ponto de entrada do Vitrolinha.
 *
 * **Andaime, não produto.** Este `main` existe para provar que o ambiente
 * está de pé: pisca um LED e imprime no console um relatório de bring-up
 * que exercita os quatro contratos compartilhados. Se o relatório sai
 * completo, as três trilhas podem trabalhar em paralelo sem esperar umas
 * pelas outras.
 *
 * É descartado inteiro quando as threads `player`, `ui`, `input` e `loader`
 * existirem — nenhuma linha daqui foi escrita para durar.
 *
 * Compila para os dois alvos: a ZBook P2 e o simulador. O que difere entre
 * eles é devicetree e configuração, não código — é o que torna possível
 * desenvolver a interface e o armazenamento sem a placa em mãos.
 */

#include <vitrolinha/cmd.h>
#include <vitrolinha/snapshot.h>
#include <vitrolinha/storage.h>
#include <vitrolinha/track.h>

#include <app_version.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

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
 * @brief Exercita o contrato do @ref storage.h e lista a biblioteca no log.
 *
 * Hoje quem responde é a implementação de mentira, com duas faixas
 * embutidas no binário. Quando o cartão entrar (issue #9), esta função não
 * muda: é o que a fronteira do @ref storage.h compra.
 *
 * @return Quantidade de faixas encontradas, ou erro negativo do contrato.
 */
static int library_report(void)
{
	static struct track_meta library[LIBRARY_MAX];
	static uint8_t buf[STORAGE_FILE_MAX];
	int count;

	LOG_INF("Cartao: %s", storage_present() ? "presente" : "ausente");

	count = storage_scan(library, ARRAY_SIZE(library));
	if (count < 0) {
		LOG_ERR("Varredura falhou: %d", count);
		return count;
	}

	LOG_INF("Biblioteca: %d faixa(s)", count);

	for (int i = 0; i < count; i++) {
		int size = storage_load(i, buf, sizeof(buf));

		if (size < 0) {
			LOG_WRN("  [%u] %-16s  carga falhou: %d", library[i].index,
				library[i].name, size);
			continue;
		}

		LOG_INF("  [%u] %-16s  %d bytes%s", library[i].index, library[i].name,
			size, library[i].valid ? "" : "  (invalida)");
	}

	return count;
}

/**
 * @brief Exercita o contrato do @ref snapshot.h.
 *
 * Publica um instantâneo de faixa tocando e o relê pelo mesmo caminho que a
 * `ui` usará, para conferir que a conversão de decorrido e de progresso
 * está de pé antes de existir tela.
 */
static void snapshot_report(void)
{
	const struct player_snapshot playing = {
		.state = PLAYER_PLAYING,
		.base_us = 0U,
		.total_us = 60U * USEC_PER_SEC,
		.track = 0,
	};
	struct player_snapshot copy;

	snapshot_publish(&playing);
	snapshot_read(&copy);

	LOG_INF("Instantaneo: faixa %d, %u us de %u us (%u/1000)", copy.track,
		snapshot_elapsed_us(&copy, 30U * USEC_PER_SEC), copy.total_us,
		snapshot_progress_permille(&copy, 30U * USEC_PER_SEC));
}

/**
 * @brief Exercita o contrato do @ref cmd.h.
 *
 * Publica e consome um comando pela fila real, no mesmo sentido em que
 * `input` e `player` vão usá-la.
 */
static void cmd_report(void)
{
	struct cmd received;
	int err;

	cmd_flush();

	err = cmd_post(CMD_SELECT, 1);
	if (err != 0) {
		LOG_ERR("Nao consegui enfileirar comando: %d", err);
		return;
	}

	err = cmd_get(&received, K_NO_WAIT);
	if (err != 0) {
		LOG_ERR("Nao consegui retirar comando: %d", err);
		return;
	}

	LOG_INF("Comando: %s faixa %d", cmd_kind_name((enum cmd_kind)received.kind),
		received.track);
}

int main(void)
{
	int err;

	LOG_INF("Vitrolinha %s em " CONFIG_BOARD_TARGET, APP_VERSION_STRING);

	err = heartbeat_init();
	if (err != 0) {
		return err;
	}

	(void)library_report();
	snapshot_report();
	cmd_report();

	LOG_INF("Bring-up completo. LED de sinal de vida piscando.");

	while (true) {
		(void)gpio_pin_toggle_dt(&heartbeat);
		k_sleep(HEARTBEAT_PERIOD);
	}

	return 0;
}

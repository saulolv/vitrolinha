/**
 * @file display.c
 * @brief Interface no OLED: tela de biblioteca e tela de reprodução.
 *
 * Duas telas, como a especificação pede:
 *
 * - **Biblioteca**: a lista de faixas e, no rodapé, um mini-player. O
 *   destaque é o foco, que só o encoder move; a faixa do instantâneo leva
 *   `>`, e a recusada leva `!`. Foco e marcador são coisas diferentes de
 *   propósito: trocar de faixa pelos botões não mexe no foco de quem está
 *   navegando. O mini-player mostra o que toca sem sair da lista.
 * - **Reprodução**: a visão detalhada — nome, estado, tempo decorrido e
 *   total, a barra e o motivo de uma carga recusada.
 *
 * Clicar numa faixa a toca e abre a reprodução. Na reprodução, girar ou
 * clicar o encoder volta à biblioteca; o botão ::DISPLAY_TOGGLE_BUTTON
 * alterna entre as duas. O transporte não depende da tela: os botões de
 * tocar, anterior e seguinte valem nas duas.
 *
 * A lista sai de @ref library_copy e o resto, de @ref snapshot_read. Nada
 * aqui sabe quem responde ao @ref storage.h — cartão na placa, faixas
 * embutidas ou pasta do computador no simulador — nem se há um player de
 * verdade do outro lado do instantâneo. A lista é relida quando a
 * @ref library_generation muda: o cartão entra e sai com a tela no ar, e
 * fora de ::LIBRARY_READY a primeira linha diz o estado do cartão.
 *
 * Este arquivo só põe a interface de pé. As telas moram em display_ui.c
 * (núcleo e navegação), display_library.c (a lista), display_mini.c (o
 * mini-player) e display_playback.c (a reprodução); o mapa está em
 * display_internal.h.
 */

#include <vitrolinha/display.h>

#include <lvgl.h>
#include <lvgl_zephyr.h>

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "display_internal.h"

/* Daqui sai a conferência de que CONFIG_LV_Z_LVGL_WORKQUEUE_PRIORITY ficou
 * onde a ordem das threads a espera.
 */
#include "priorities.h"

LOG_MODULE_REGISTER(display, LOG_LEVEL_INF);

/**
 * @brief Dispositivo de display definido pelo nó chosen do Devicetree.
 */
static const struct device *const display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

int display_init(void)
{
	int err;

	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display nao esta pronto");
		return -ENODEV;
	}

	ui_prepare();

	/* A função pode rodar em paralelo com o laço periódico do LVGL; o lock
	 * protege a criação e a configuração dos objetos.
	 */
	lvgl_lock();
	ui_build();
	lvgl_unlock();

	err = display_blanking_off(display_dev);
	if ((err != 0) && (err != -ENOSYS)) {
		LOG_ERR("Nao consegui desligar o blanking do display: %d", err);
		return err;
	}

	LOG_INF("Display no ar (%s)", display_dev->name);

	return 0;
}

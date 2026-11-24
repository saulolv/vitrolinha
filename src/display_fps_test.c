/**
 * @file display_fps_test.c
 * @brief Teste de desempenho do display, no lugar da interface.
 *
 * Só entra no build com `CONFIG_VITROLINHA_DISPLAY_FPS_TEST`: substitui as
 * duas telas por uma de demonstração que invalida a tela inteira a cada
 * quadro e registra no log a taxa de flushes. É o pior caso do barramento —
 * quadro cheio de 1024 bytes a cada vez —, para medir o I²C e não o
 * desenho por região suja da interface de verdade.
 *
 * Para ligar: `scripts/build.sh --pristine -- -DCONFIG_VITROLINHA_DISPLAY_FPS_TEST=y`.
 */

#include <vitrolinha/display.h>

#include <lvgl.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "display_internal.h"

LOG_MODULE_DECLARE(display, LOG_LEVEL_INF);

/** @brief Período do quadro de demonstração: os 10 fps do plano. */
#define DISPLAY_DEMO_PERIOD_MS 100

static lv_obj_t *frame_label;
static uint32_t frame_counter;
static uint32_t fps_frame_count;
static int64_t fps_window_start_ms;

/**
 * @brief Conta os flushes concluídos e registra a taxa a cada segundo.
 *
 * É flushes por segundo; vira FPS só quando cada quadro é um flush. Roda no
 * contexto do LVGL, com o lock já adquirido — não o adquirir de novo aqui.
 */
static void display_flush_event_cb(lv_event_t *e)
{
	ARG_UNUSED(e);

	int64_t now_ms = k_uptime_get();
	int64_t elapsed_ms;

	if (fps_window_start_ms == 0) {
		fps_window_start_ms = now_ms;
	}

	fps_frame_count++;
	elapsed_ms = now_ms - fps_window_start_ms;

	if (elapsed_ms >= 1000) {
		uint32_t fps_x10 = (uint32_t)((fps_frame_count * 10000) / elapsed_ms);

		LOG_INF("Taxa de quadros real: %u.%u fps (%u quadros em %u ms)", fps_x10 / 10,
			fps_x10 % 10, fps_frame_count, (uint32_t)elapsed_ms);

		fps_frame_count = 0;
		fps_window_start_ms = now_ms;
	}
}

/** @brief Atualiza o contador e invalida a tela inteira. */
static void display_demo_timer_cb(lv_timer_t *timer)
{
	ARG_UNUSED(timer);

	frame_counter++;
	lv_label_set_text_fmt(frame_label, "quadro %u", frame_counter);
	lv_obj_invalidate(lv_screen_active());
}

/** @brief Nada a preparar: a demonstração não mostra a biblioteca. */
void ui_prepare(void)
{
}

void ui_build(void)
{
	lv_obj_t *title = lv_label_create(lv_screen_active());

	lv_label_set_text(title, "Vitrola");
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

	frame_label = lv_label_create(lv_screen_active());
	lv_label_set_text(frame_label, "quadro 0");
	lv_obj_align(frame_label, LV_ALIGN_BOTTOM_MID, 0, 0);

	lv_display_add_event_cb(lv_display_get_default(), display_flush_event_cb,
				LV_EVENT_FLUSH_FINISH, NULL);
	lv_timer_create(display_demo_timer_cb, DISPLAY_DEMO_PERIOD_MS, NULL);

	/* Pede o primeiro quadro antes de desligar o blanking. */
	lv_timer_handler();
}

void display_cue(int track)
{
	ARG_UNUSED(track);
}

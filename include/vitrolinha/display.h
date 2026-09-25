#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <lvgl.h>
#include <lvgl_input_device.h>
#include <lvgl_zephyr.h>

/** @brief Período do quadro de demonstração: os 10 fps do plano. */
#define DISPLAY_DEMO_PERIOD_MS 100

/**
 * @brief Inicializa a demonstração do display.
 *
 * Cria o rótulo, configura o callback de flush e inicia o timer
 * responsável por forçar um quadro completo a cada ciclo.
 *
 * @return 0 em caso de sucesso.
 * @return Valor negativo em caso de erro.
 */
static void lv_music_roller(void);
int display_init(void);
void lv_example_get_started_button(void);

#endif // DISPLAY_H_

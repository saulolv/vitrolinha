/**
 * @file fake_display.c
 * @brief Display de teste: 128x64, monocromático, guarda o último quadro.
 *
 * Existe porque o `zephyr,dummy-dc` sempre aceita desligar o blanking, e a
 * interface tem um caminho para cada resposta: sucesso, -ENOSYS e erro. O
 * quadro guardado deixa os testes conferirem o desenho pixel a pixel, sem a
 * placa, e imprimi-lo para quem quiser olhar.
 */

#define DT_DRV_COMPAT vnd_fake_display

#include "fake_display.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>

#define WIDTH  DT_INST_PROP(0, width)
#define HEIGHT DT_INST_PROP(0, height)

/**
 * @brief O quadro, no formato do SSD1306: páginas de 8 linhas, um byte por
 *        coluna, o bit 0 no alto. Em MONO01 o bit 1 é branco.
 */
static uint8_t frame[HEIGHT / 8][WIDTH];

static int blanking_off_result;

void fake_display_set_blanking_off_result(int result)
{
	blanking_off_result = result;
}

bool fake_display_lit(int x, int y)
{
	/* Branco no LVGL é bit 1 em MONO01, e sai apagado no OLED. */
	return ((frame[y / 8][x] >> (y % 8)) & 1U) == 0U;
}

void fake_display_dump(const char *title)
{
	if (getenv("VITROLINHA_TELA") == NULL) {
		return;
	}

	printk("=== %s\n", title);
	for (int y = 0; y < HEIGHT; y++) {
		char line[WIDTH + 1];

		for (int x = 0; x < WIDTH; x++) {
			line[x] = fake_display_lit(x, y) ? '#' : '.';
		}
		line[WIDTH] = '\0';
		printk("%02d %s\n", y, line);
	}
}

static int fake_blanking_on(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int fake_blanking_off(const struct device *dev)
{
	ARG_UNUSED(dev);
	return blanking_off_result;
}

static int fake_write(const struct device *dev, const uint16_t x, const uint16_t y,
		      const struct display_buffer_descriptor *desc, const void *buf)
{
	const uint8_t *in = buf;

	ARG_UNUSED(dev);

	/* O LVGL arredonda as áreas a páginas inteiras, então y é múltiplo de 8. */
	for (uint16_t page = 0; page < desc->height / 8U; page++) {
		memcpy(&frame[y / 8U + page][x], &in[page * desc->pitch], desc->width);
	}
	return 0;
}

static void fake_get_capabilities(const struct device *dev,
				  struct display_capabilities *capabilities)
{
	ARG_UNUSED(dev);

	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->x_resolution = WIDTH;
	capabilities->y_resolution = HEIGHT;
	capabilities->supported_pixel_formats = PIXEL_FORMAT_MONO01;
	capabilities->current_pixel_format = PIXEL_FORMAT_MONO01;
	capabilities->screen_info = SCREEN_INFO_MONO_VTILED;
}

static int fake_set_pixel_format(const struct device *dev,
				 const enum display_pixel_format format)
{
	ARG_UNUSED(dev);
	return format == PIXEL_FORMAT_MONO01 ? 0 : -ENOTSUP;
}

static DEVICE_API(display, fake_display_api) = {
	.blanking_on = fake_blanking_on,
	.blanking_off = fake_blanking_off,
	.write = fake_write,
	.get_capabilities = fake_get_capabilities,
	.set_pixel_format = fake_set_pixel_format,
};

static int fake_display_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

DEVICE_DT_INST_DEFINE(0, fake_display_init, NULL, NULL, NULL, POST_KERNEL,
		      CONFIG_DISPLAY_INIT_PRIORITY, &fake_display_api);

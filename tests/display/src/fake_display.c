/**
 * @file fake_display.c
 * @brief Display de teste: 128x64, monocromático, descarta o que recebe.
 *
 * Existe porque o `zephyr,dummy-dc` sempre aceita desligar o blanking, e a
 * interface tem um caminho para cada resposta: sucesso, -ENOSYS e erro.
 */

#define DT_DRV_COMPAT vnd_fake_display

#include "fake_display.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

static int blanking_off_result;

void fake_display_set_blanking_off_result(int result)
{
	blanking_off_result = result;
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
	ARG_UNUSED(dev);
	ARG_UNUSED(x);
	ARG_UNUSED(y);
	ARG_UNUSED(desc);
	ARG_UNUSED(buf);
	return 0;
}

static void fake_get_capabilities(const struct device *dev,
				  struct display_capabilities *capabilities)
{
	ARG_UNUSED(dev);

	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->x_resolution = DT_INST_PROP(0, width);
	capabilities->y_resolution = DT_INST_PROP(0, height);
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

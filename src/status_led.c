/**
 * @file status_led.c
 * @brief Implementação de @ref status_led.h.
 */
#include <vitrolinha/status_led.h>

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#define STRIP_NODE DT_ALIAS(led_strip)

/** @brief Quantos LEDs a fita tem: quatro na ZBook P2. */
#define STRIP_LENGTH DT_PROP(STRIP_NODE, chain_length)

/** @brief Pilha da thread: uma leitura do instantâneo e uma escrita na fita. */
#define STATUS_LED_STACK_SIZE 1024

/**
 * @brief Prioridade 11: abaixo da tela (10), acima do monitor do cartão (12).
 *
 * A cor atrasada de um quadro não é defeito; a nota atrasada é.
 */
#define STATUS_LED_PRIORITY 11

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);

static struct led_rgb pixels[STRIP_LENGTH];

/** @brief Nenhuma cor mostrada: fora dos 24 bits de @ref pack. */
#define SHOWN_NONE UINT32_MAX

/** @brief A cor que a fita mostra, por @ref pack; ::SHOWN_NONE antes da primeira. */
static uint32_t shown = SHOWN_NONE;

static uint32_t pack(struct led_rgb color)
{
	return ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | color.b;
}

static struct led_rgb rgb(uint8_t r, uint8_t g, uint8_t b)
{
	return (struct led_rgb){.r = r, .g = g, .b = b};
}

struct led_rgb status_led_color(const struct player_snapshot *snap, enum library_state library)
{
	if (snap->state == PLAYER_PLAYING) {
		return rgb(0, STATUS_LED_LEVEL, 0);
	}
	if (snap->state == PLAYER_PAUSED) {
		/* Âmbar: vermelho cheio e metade de verde. */
		return rgb(STATUS_LED_LEVEL, STATUS_LED_LEVEL / 2U, 0);
	}
	if ((snap->load_error != 0) || (library != LIBRARY_READY)) {
		return rgb(STATUS_LED_LEVEL, 0, 0);
	}
	return rgb(0, 0, 0);
}

/** @brief Uma passada: lê o instantâneo e reescreve a fita se a cor mudou. */
static void status_led_update(void)
{
	struct player_snapshot snap;

	snapshot_read(&snap);
	struct led_rgb color = status_led_color(&snap, library_state());

	if (pack(color) == shown) {
		return;
	}
	for (size_t i = 0; i < ARRAY_SIZE(pixels); i++) {
		pixels[i] = color;
	}
	/* Falhou, a cor fica inválida e a passada seguinte tenta de novo. */
	shown = (led_strip_update_rgb(strip, pixels, ARRAY_SIZE(pixels)) == 0) ? pack(color)
										 : SHOWN_NONE;
}

static void status_led_worker(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (;;) {
		status_led_update();
		k_msleep(STATUS_LED_PERIOD_MS);
	}
}

K_THREAD_DEFINE(status_led_thread, STATUS_LED_STACK_SIZE, status_led_worker, NULL, NULL, NULL,
		STATUS_LED_PRIORITY, 0, SYS_FOREVER_MS);

int status_led_init(void)
{
	if (!device_is_ready(strip)) {
		return -ENODEV;
	}
	k_thread_start(status_led_thread);
	return 0;
}

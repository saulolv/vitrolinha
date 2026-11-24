/** @file fake_strip.c
 * @brief Driver da fita de mentira, no nó `vnd,fake-led-strip`.
 */
#define DT_DRV_COMPAT vnd_fake_led_strip

#include "fake_strip.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

BUILD_ASSERT(DT_INST_PROP(0, chain_length) == FAKE_STRIP_LENGTH);

static struct k_spinlock lock;
static struct led_rgb last[FAKE_STRIP_LENGTH];
static size_t last_count;
static unsigned int updates;
static int result;

static int fake_update_rgb(const struct device *dev, struct led_rgb *pixels, size_t count)
{
	ARG_UNUSED(dev);
	k_spinlock_key_t key = k_spin_lock(&lock);

	last_count = MIN(count, FAKE_STRIP_LENGTH);
	memcpy(last, pixels, last_count * sizeof(last[0]));
	updates++;
	int err = result;

	k_spin_unlock(&lock, key);
	return err;
}

static size_t fake_length(const struct device *dev)
{
	ARG_UNUSED(dev);
	return FAKE_STRIP_LENGTH;
}

static DEVICE_API(led_strip, fake_api) = {
	.update_rgb = fake_update_rgb,
	.length = fake_length,
};

static int fake_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

DEVICE_DT_INST_DEFINE(0, fake_init, NULL, NULL, NULL, POST_KERNEL,
		      CONFIG_LED_STRIP_INIT_PRIORITY, &fake_api);

unsigned int fake_strip_updates(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	unsigned int n = updates;

	k_spin_unlock(&lock, key);
	return n;
}

size_t fake_strip_last(struct led_rgb out[FAKE_STRIP_LENGTH])
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	size_t n = last_count;

	memcpy(out, last, sizeof(last));
	k_spin_unlock(&lock, key);
	return n;
}

void fake_strip_set_result(int value)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	result = value;
	k_spin_unlock(&lock, key);
}

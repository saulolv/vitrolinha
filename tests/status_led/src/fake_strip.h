/** @file fake_strip.h
 * @brief Fita de LEDs de mentira: guarda a última escrita.
 */
#ifndef FAKE_STRIP_H_
#define FAKE_STRIP_H_

#include <stddef.h>
#include <zephyr/drivers/led_strip.h>

/** @brief LEDs da fita, como no devicetree. */
#define FAKE_STRIP_LENGTH 4U

/** @brief Quantas escritas a fita recebeu, com sucesso ou não. */
unsigned int fake_strip_updates(void);

/** @brief Cópia da última escrita; devolve quantos LEDs ela tinha. */
size_t fake_strip_last(struct led_rgb out[FAKE_STRIP_LENGTH]);

/** @brief O que as próximas escritas devolvem; 0 é sucesso. */
void fake_strip_set_result(int result);

#endif /* FAKE_STRIP_H_ */

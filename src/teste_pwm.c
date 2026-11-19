#include <vitrolinha/tone.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>


// Renomeie o arquivo para main.c e mude o nome da main original

int main(void)
{
	/* Trecho inicial de Fur Elise, simplificado para uma voz no buzzer.
	 * Frequencias em Hz e duracoes em ms; zero representa silencio.
	 */
	const struct {
		uint32_t frequency_hz;
		uint32_t duration_ms;
	} notes[] = {
		{659, 180}, {622, 180}, {659, 180}, {622, 180}, /* Mi5, Re#5 */
		{659, 180}, {494, 180}, {587, 180}, {523, 180}, /* Mi5, Si4, Re5, Do5 */
		{440, 540}, {0, 180},                         /* La4 */
		{262, 180}, {330, 180}, {440, 180}, {494, 540}, /* Do4, Mi4, La4, Si4 */
		{0, 180},
		{330, 180}, {415, 180}, {494, 180}, {523, 540}, /* Mi4, Sol#4, Si4, Do5 */
		{0, 180},
		{330, 180}, {659, 180}, {622, 180}, {659, 180},
		{622, 180}, {659, 180}, {494, 180}, {587, 180},
		{523, 180}, {440, 720}, {0, 180},
	};
	int err = tone_init();

	if (err != 0) {
		printk("tone_init falhou: %d\n", err);
		return err;
	}
	for (size_t i = 0; i < ARRAY_SIZE(notes); i++) {
		err = tone_set_frequency(notes[i].frequency_hz);
		if (err != 0) {
			(void)tone_set_frequency(0);
			printk("tone_set_frequency falhou: %d\n", err);
			return err;
		}
		k_msleep(notes[i].duration_ms);
	}
	return 0;
}

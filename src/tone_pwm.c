/**
 * @file tone_pwm.c
 * @brief Aplica frequência ou silêncio ao PWM do buzzer da ZBook.
 *
 * Usa o clock informado pelo driver e pulso de aproximadamente 50%.
 * Não controla durações nem cria timers. Após tone_init(), a aplicação deve
 * serializar as chamadas, que podem ocorrer em contexto de interrupção.
 */
#include <vitrolinha/tone.h>

#include <errno.h>
#include <zephyr/drivers/pwm.h>

static const struct pwm_dt_spec buzzer = PWM_DT_SPEC_GET(DT_ALIAS(pwm_buzzer));
static uint64_t cycles_per_sec;
/* Período válido também antes de qualquer nota; pulso zero mantém silêncio. */
static uint32_t last_period = 2;

int tone_init(void)
{
	if (!pwm_is_ready_dt(&buzzer)) {
		return -ENODEV;
	}
	int err = pwm_get_cycles_per_sec(buzzer.dev, buzzer.channel, &cycles_per_sec);

	if (err != 0) {
		return err;
	}
	return tone_set_frequency(0);
}

int tone_set_frequency(uint32_t frequency_hz)
{
	uint64_t period = last_period;
	uint32_t pulse = 0;

	if (frequency_hz != 0) {
		period = (cycles_per_sec + frequency_hz / 2U) / frequency_hz;
		if (period < 2 || period > UINT32_MAX) {
			return -ERANGE;
		}
		pulse = period / 2U;
	}
	int err = pwm_set_cycles(buzzer.dev, buzzer.channel, period, pulse, buzzer.flags);

	if (err == 0) {
		last_period = period;
	}
	/* No driver da placa, EINVAL também indica período não representável. */
	return err == -EINVAL ? -ERANGE : err;
}

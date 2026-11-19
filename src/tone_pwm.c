/**
 * @file tone_pwm.c
 * @brief O @ref tone.h servido pelo buzzer da ZBook, por PWM.
 *
 * Vem do `tone_pwm.c` do branch `final-version`.
 *
 * Duas regras do projeto moram aqui.
 *
 * **O período sai de `pwm_get_cycles_per_sec()`**, nunca de constante, e a
 * escrita é `pwm_set_cycles`, não `pwm_set`: esta converteria nanossegundos
 * em ciclos truncando, e o arredondamento seria pago duas vezes. Com o
 * overlay da placa o slice opera em modo de divisor automático, e o driver
 * informa o relógio do periférico (150 MHz); o divisor de cada nota é ele
 * quem escolhe.
 *
 * **Silêncio é pulso zero, nunca período zero** — período zero é ajuste
 * inválido de PWM no Zephyr. O silêncio mantém o período da última nota.
 *
 * Seguro em interrupção: no rp2xxx, `pwm_set_cycles` são escritas em
 * registrador, sem trava, log ou espera. O relógio é lido uma vez, em
 * @ref tone_init, fora do callback.
 */

#include <vitrolinha/tone.h>

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>

/**
 * @brief Faixa de frequências aceitas, em hertz.
 *
 * Abaixo de 20 Hz não é nota; acima de 20 kHz ninguém ouve, e o piezo já
 * não responde muito antes. O limite de baixo do driver fica mais abaixo
 * ainda (~9 Hz, com divisor 256 e contador de 16 bits), então a faixa daqui
 * é a que vale.
 */
#define TONE_FREQ_MIN_HZ 20U
#define TONE_FREQ_MAX_HZ 20000U

/**
 * @brief Relógio mínimo do canal: dois ciclos por período na nota mais aguda.
 *
 * Abaixo disso a nota mais aguda teria período menor que 2, sem espaço para
 * pulso e vale. Na placa o relógio é 150 MHz, e com ele qualquer nota da
 * faixa acima tem período entre 7 500 e 7 500 000 ciclos.
 */
#define TONE_CLOCK_MIN_HZ (2U * TONE_FREQ_MAX_HZ)

static const struct pwm_dt_spec buzzer = PWM_DT_SPEC_GET(DT_ALIAS(pwm_buzzer));

/** @brief Relógio do canal, lido uma vez em @ref tone_init. */
static uint64_t cycles_per_sec;

/** @brief Período em vigor; o silêncio o reaproveita. Válido desde o início. */
static uint32_t last_period = 2U;

int tone_init(void)
{
	if (!pwm_is_ready_dt(&buzzer)) {
		return -ENODEV;
	}

	/* Uma falha do driver deixa o relógio em zero, que a faixa abaixo
	 * recusa como qualquer outro relógio inútil.
	 */
	cycles_per_sec = 0U;
	(void)pwm_get_cycles_per_sec(buzzer.dev, buzzer.channel, &cycles_per_sec);

	/* Uma comparação só cobre os dois lados: abaixo do mínimo a subtração
	 * dá a volta e passa do teto. Com o relógio de 32 bits, o período de
	 * qualquer nota cabe no uint32_t do pwm_set_cycles.
	 */
	if ((cycles_per_sec - TONE_CLOCK_MIN_HZ) > ((uint64_t)UINT32_MAX - TONE_CLOCK_MIN_HZ)) {
		return -ENOTSUP;
	}

	return tone_set_frequency(0U);
}

int tone_set_frequency(uint32_t frequency_hz)
{
	uint32_t period = last_period;
	uint32_t pulse = 0U;
	int err;

	if (frequency_hz != 0U) {
		if ((frequency_hz < TONE_FREQ_MIN_HZ) || (frequency_hz > TONE_FREQ_MAX_HZ)) {
			return -ERANGE;
		}

		/* Arredondado ao ciclo mais próximo, não truncado. O relógio
		 * conferido no tone_init garante período de 2 a UINT32_MAX / 20.
		 */
		period = (uint32_t)((cycles_per_sec + (frequency_hz / 2U)) / frequency_hz);

		/* 50%: onde o piezo é mais alto, segundo a bring-up do fabricante. */
		pulse = period / 2U;
	}

	err = pwm_set_cycles(buzzer.dev, buzzer.channel, period, pulse, buzzer.flags);
	if (err == 0) {
		last_period = period;
	}

	/* No rp2xxx, -EINVAL também é período que o divisor não alcança. */
	return (err == -EINVAL) ? -ERANGE : err;
}

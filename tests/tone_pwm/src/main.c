/**
 * @file main.c
 * @brief Testes do @ref tone.h servido por PWM (src/tone_pwm.c).
 *
 * O PWM é o de mentira do Zephyr, que guarda os argumentos de cada
 * `pwm_set_cycles`. O que se verifica é a aritmética que a placa não mostra:
 * o período sai do relógio que o driver informa, arredondado; o pulso é a
 * metade; o silêncio é pulso zero sobre o último período válido, nunca
 * período zero.
 */

#include <vitrolinha/tone.h>

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm/pwm_fake.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

DEFINE_FFF_GLOBALS;

#define FAKE_PWM_NODE DT_NODELABEL(fake_pwm)

/** @brief Canal do buzzer no app.overlay. */
#define BUZZER_CHANNEL 16U

/** @brief Relógio do cenário; o lento é o de relogio_lento.overlay. */
#define CLOCK_HZ DT_PROP(FAKE_PWM_NODE, frequency)

/** @brief Abaixo disto o tone_init recusa o relógio. */
#define CLOCK_MIN_HZ 40000U

#define SLOW_CLOCK (CLOCK_HZ < CLOCK_MIN_HZ)

/** @brief Confere a escrita @p i: período e pulso. */
static void expect_write(unsigned int i, uint32_t period, uint32_t pulse)
{
	zassert_true(i < fake_pwm_set_cycles_fake.call_count, "escrita %u nao aconteceu", i);
	zassert_equal(BUZZER_CHANNEL, fake_pwm_set_cycles_fake.arg1_history[i]);
	zassert_equal(period, fake_pwm_set_cycles_fake.arg2_history[i], "periodo da escrita %u",
		      i);
	zassert_equal(pulse, fake_pwm_set_cycles_fake.arg3_history[i], "pulso da escrita %u", i);
}

/**
 * @brief Inicializa a saída, conferindo antes a recusa do PWM que não subiu.
 *
 * Feito uma vez para a suíte: a regra do PWM de mentira zera o histórico
 * antes de cada caso, então o que a inicialização escreveu é conferido aqui.
 */
static void *tone_pwm_setup(void)
{
	const struct device *pwm = DEVICE_DT_GET(FAKE_PWM_NODE);

	int ret = tone_init();

	zassert_equal(-ENODEV, ret, "o PWM ainda nao inicializou: %d", ret);
	zassert_ok(device_init(pwm));

	if (SLOW_CLOCK) {
		ret = tone_init();
		zassert_equal(-ENOTSUP, ret, "relogio de %u Hz aceito: %d", CLOCK_HZ, ret);
		zassert_equal(0U, fake_pwm_set_cycles_fake.call_count,
			      "relogio recusado, mas o PWM foi programado");
		return NULL;
	}

	ret = tone_init();
	zassert_ok(ret, "tone_init: %d", ret);

	/* Sai em silêncio, com um período válido desde a primeira escrita. */
	zassert_equal(1U, fake_pwm_set_cycles_fake.call_count);
	expect_write(0U, 2U, 0U);

	return NULL;
}

static void tone_pwm_before(void *fixture)
{
	ARG_UNUSED(fixture);

	if (SLOW_CLOCK) {
		ztest_test_skip();
	}
}

ZTEST_SUITE(tone_pwm, NULL, tone_pwm_setup, tone_pwm_before, NULL, NULL);

ZTEST(tone_pwm, test_la_440)
{
	/* 150 MHz / 440 = 340 909,09: o período que a placa programa. */
	zassert_ok(tone_set_frequency(440U));
	expect_write(0U, 340909U, 170454U);
}

ZTEST(tone_pwm, test_periodo_arredonda_e_nao_trunca)
{
	/* 150 MHz / 27 = 5 555 555,56: truncar daria um ciclo a menos. */
	zassert_ok(tone_set_frequency(27U));
	expect_write(0U, 5555556U, 2777778U);
}

ZTEST(tone_pwm, test_silencio_e_pulso_zero_no_ultimo_periodo)
{
	zassert_ok(tone_set_frequency(1000U));
	zassert_ok(tone_set_frequency(0U));

	expect_write(1U, 150000U, 0U);
}

ZTEST(tone_pwm, test_limites_da_faixa)
{
	zassert_ok(tone_set_frequency(20U));
	zassert_ok(tone_set_frequency(20000U));

	expect_write(0U, 7500000U, 3750000U);
	expect_write(1U, 7500U, 3750U);
}

ZTEST(tone_pwm, test_fora_da_faixa_nao_toca_no_pwm)
{
	zassert_equal(-ERANGE, tone_set_frequency(19U));
	zassert_equal(-ERANGE, tone_set_frequency(20001U));

	zassert_equal(0U, fake_pwm_set_cycles_fake.call_count);
}

ZTEST(tone_pwm, test_periodo_recusado_pelo_driver_e_erange)
{
	zassert_ok(tone_set_frequency(1000U));

	/* No rp2xxx, -EINVAL é período que o divisor não alcança. */
	fake_pwm_set_cycles_fake.return_val = -EINVAL;
	zassert_equal(-ERANGE, tone_set_frequency(440U));

	/* O período recusado não vira o do silêncio. */
	fake_pwm_set_cycles_fake.return_val = 0;
	zassert_ok(tone_set_frequency(0U));
	expect_write(2U, 150000U, 0U);
}

ZTEST(tone_pwm, test_outro_erro_do_driver_passa_adiante)
{
	fake_pwm_set_cycles_fake.return_val = -EIO;

	zassert_equal(-EIO, tone_set_frequency(440U));
}

ZTEST_SUITE(tone_pwm_lento, NULL, NULL, NULL, NULL, NULL);

ZTEST(tone_pwm_lento, test_cenario)
{
	/* A recusa do relógio lento é conferida no setup da outra suíte; aqui
	 * só se registra em qual cenário a execução está.
	 */
	TC_PRINT("relogio do PWM: %u Hz%s\n", CLOCK_HZ, SLOW_CLOCK ? " (lento)" : "");
}

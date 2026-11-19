/**
 * @file tone_trace.c
 * @brief Saída simulada: registra cada aplicação de tom, sem produzir áudio.
 */
#include <vitrolinha/tone.h>
#include <vitrolinha/playback_trace.h>

#include <errno.h>

#include <zephyr/kernel.h>

int tone_init(void)
{
	/* Não há periférico a configurar nem captura a iniciar. */
	return 0;
}

int tone_set_frequency(uint32_t frequency_hz)
{
	/* A mesma faixa do buzzer: uma frequência que a placa recusaria não
	 * pode passar no simulador e esconder o defeito da faixa.
	 */
	if (!tone_frequency_supported(frequency_hz)) {
		return -ERANGE;
	}

	/* Ciclos observados, não o instante nominal do callback de timeout. */
	return playback_trace_record(k_cyc_to_us_floor64(k_cycle_get_64()), frequency_hz);
}

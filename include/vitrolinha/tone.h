/**
 * @file tone.h
 * @brief Contrato: aplicação de frequência ou silêncio à saída musical.
 *
 * Interface única para tone_trace.c no simulador e, futuramente, uma
 * implementação PWM na placa. Apenas uma implementação será selecionada
 * por build. O módulo não decide a duração do tom; isso cabe ao player.
 *
 * No simulador, uma captura deve estar aberta antes de aplicar tons.
 */

#ifndef VITROLINHA_TONE_H_
#define VITROLINHA_TONE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Faixa de frequências que toda saída aceita, em hertz.
 *
 * Abaixo de 20 Hz não é nota; acima de 20 kHz ninguém ouve, e o piezo já não
 * responde muito antes. Mora no contrato, e não na implementação PWM, porque
 * o parser também a usa: uma faixa com frequência fora dela é recusada na
 * carga, com a linha do erro, em vez de parar no meio da reprodução quando a
 * nota chega à saída. O simulador aplica a mesma faixa que a placa.
 */
#define TONE_FREQ_MIN_HZ 20U
#define TONE_FREQ_MAX_HZ 20000U

/**
 * @brief Diz se @p frequency_hz é silêncio ou cabe na faixa da saída.
 */
static inline bool tone_frequency_supported(uint32_t frequency_hz)
{
	return (frequency_hz == 0U) ||
	       ((frequency_hz >= TONE_FREQ_MIN_HZ) && (frequency_hz <= TONE_FREQ_MAX_HZ));
}

/**
 * @brief Prepara a saída e a mantém silenciosa, sem registrar evento.
 * @return 0 no sucesso ou erro negativo de inicialização.
 * @note Executar uma vez, em contexto de thread, antes do player.
 */
int tone_init(void);

/**
 * @brief Aplica uma frequência até a próxima chamada.
 * @param frequency_hz Frequência em hertz; zero solicita silêncio.
 * @return 0 no sucesso; -ERANGE para frequência fora de
 *         ::TONE_FREQ_MIN_HZ .. ::TONE_FREQ_MAX_HZ; outros
 *         erros negativos da saída, inclusive -ENOSPC no registro simulado.
 * @note Chamável em interrupção, sem espera, E/S de arquivo ou alocação.
 *       O lado do player serializa chamadas da thread e do timer.
 *       No PWM, silêncio será pulso zero com período válido.
 */
int tone_set_frequency(uint32_t frequency_hz);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_TONE_H_ */

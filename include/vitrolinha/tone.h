/**
 * @file tone.h
 * @brief Contrato: aplicação de frequência ou silêncio à saída musical.
 *
 * Interface única para tone_trace.c no simulador e, futuramente, uma
 * implementação PWM na placa. Apenas uma implementação será selecionada
 * por build. O módulo não decide a duração do tom; isso cabe ao player.
 *
 * Somente declarações; ainda não produz som nem registra eventos.
 */

#ifndef VITROLINHA_TONE_H_
#define VITROLINHA_TONE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Prepara a saída e a mantém silenciosa, sem registrar evento.
 * @return 0 no sucesso ou erro negativo de inicialização.
 * @note Executar uma vez, em contexto de thread, antes do player.
 */
int tone_init(void);

/**
 * @brief Aplica uma frequência até a próxima chamada.
 * @param frequency_hz Frequência em hertz; zero solicita silêncio.
 * @return 0 no sucesso; -ERANGE para frequência não suportada; outros
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

/**
 * @file tone_trace.c
 * @brief Esqueleto da saída de tons por registro no simulador.
 *
 * Implementará tone.h sem PWM nem áudio ao vivo. Cada aplicação de
 * frequência, inclusive repetida ou zero, será registrada por
 * playback_trace_record com o instante observado no relógio do Zephyr.
 * Não gravará arquivos dentro da chamada de saída.
 *
 * A captura deverá ser iniciada antes da reprodução e encerrada depois
 * de parar o timer e aplicar silêncio. Sua preparação/exportação será
 * coordenada fora do caminho temporal do player.
 *
 * Somente assinaturas; não há implementação nem integração ao build.
 * Futuramente tone_pwm.c implementará o mesmo contrato para a placa.
 */

#include <vitrolinha/tone.h>

/**
 * @brief A implementar: preparar a saída simulada sem iniciar uma captura.
 * @see tone.h para contexto de chamada e retorno.
 */
int tone_init(void);

/**
 * @brief A implementar: registrar frequência e instante, sem bloquear.
 * @see tone.h para parâmetros, erros e serialização das chamadas.
 * @note Propagar erro do registro, incluindo captura fechada ou incompleta.
 */
int tone_set_frequency(uint32_t frequency_hz);

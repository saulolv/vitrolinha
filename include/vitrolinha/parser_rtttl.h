/**
 * @file parser_rtttl.h
 * @brief Contrato: interpretação de RTTTL a partir de um buffer imutável.
 *
 * RTTTL é o formato dos toques de celular da Nokia, uma linha só:
 *
 * ```
 * Fur Elise:d=8,o=5,b=125:e6,d#6,e6,d#6,e6,b,d6,c6,4a.,p
 * ^ nome     ^ padrões      ^ notas
 * ```
 *
 * Cada nota é `[duração]letra[#][.][oitava][.]`: duração 1, 2, 4, 8, 16 ou
 * 32 (a fração da semibreve), letra de `a` a `g` (`h` vale `b`) ou `p` para
 * pausa, `#` sustenido, `.` pontuada (vale uma vez e meia), oitava de 1 a 8.
 * O que falta vem dos padrões `d`, `o` e `b` (andamento em semínimas por
 * minuto), que valem 4, 6 e 63 quando o cabeçalho os omite. Espaços e
 * quebras de linha entre os campos são aceitos.
 *
 * **O tempo sai de uma grade inteira**, e não da soma de durações em
 * milissegundos. O parser conta `n64`, as sexagésimas-quartas desde o
 * início da faixa, e converte cada instante com uma conta só:
 * `n64 * 3750000 / bpm` µs. O erro é um truncamento por evento, menor que
 * 1 µs, que não acumula. Em sexagésimas-quartas toda duração do RTTTL cai em
 * inteiro, a semifusa pontuada (`32.`, 3/64) inclusive.
 *
 * **Cada nota gera dois eventos**: o tom no início da fatia e o silêncio de
 * articulação, `min(fatia / 10, 20 ms)` antes do fim dela. Sem o silêncio,
 * duas notas iguais seguidas soariam como uma só. A pausa é a fatia inteira
 * em silêncio, e não gera evento quando o anterior já é silêncio.
 *
 * Sem E/S nem alocação; chamar fora do callback do timer.
 */

#ifndef VITROLINHA_PARSER_RTTTL_H_
#define VITROLINHA_PARSER_RTTTL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <vitrolinha/event.h>
#include <vitrolinha/parser_csv.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Iterador de uma faixa RTTTL validada.
 *
 * Os campos são do parser; quem usa só lê @ref total_us e
 * @ref final_event_emitted.
 */
struct parser_rtttl {
	const uint8_t *buffer;
	size_t length;
	/** @brief Onde começam as notas, depois do segundo `:`. */
	size_t notes_start;
	/** @brief Próximo byte a interpretar. */
	size_t position;
	uint8_t default_duration;
	uint8_t default_octave;
	uint16_t bpm;
	/** @brief Oitavas somadas a toda nota pela transposição. */
	uint8_t shift;
	/** @brief Sexagésimas-quartas desde o início, no começo da próxima nota. */
	uint32_t n64;
	/** @brief Instante do silêncio final, que é a duração da faixa. */
	uint32_t total_us;
	/** @brief Notas e pausas da faixa, contadas na validação. */
	size_t note_count;
	/** @brief Silêncio de articulação ainda por entregar, e quando. */
	bool pending_silence;
	uint32_t pending_offset_us;
	/** @brief O último evento entregue foi silêncio? */
	bool last_silent;
	bool final_event_emitted;
};

/**
 * @brief Valida a faixa inteira e prepara o iterador no primeiro evento.
 *
 * @param parser Estado de destino; zerado em caso de falha.
 * @param buffer RTTTL completo; não precisa terminar em NUL.
 * @param length Quantidade exata de bytes de @p buffer.
 * @param error  Linha e coluna, a partir de 1, do primeiro byte errado; pode
 *               ser NULL. Zerado em caso de sucesso.
 * @param transpose_min_hz Limiar da transposição: a faixa sobe em oitavas
 *               inteiras até a nota mais grave alcançá-lo, sem que a mais
 *               aguda passe de ::TONE_FREQ_MAX_HZ. Zero desliga.
 * @retval 0       Faixa válida.
 * @retval -EINVAL Argumento inválido, ou formato: cabeçalho sem os dois `:`,
 *                 padrão desconhecido, nota malformada, faixa sem notas.
 * @retval -ERANGE Valor fora da faixa: andamento, oitava, frequência que a
 *                 saída não toca, ou faixa mais longa que 32 bits de µs.
 */
int parser_rtttl_init(struct parser_rtttl *parser, const uint8_t *buffer, size_t length,
		      struct parser_csv_error *error, uint32_t transpose_min_hz);

/**
 * @brief Obtém o próximo evento, incluindo o silêncio final da faixa.
 *
 * @return 1 para evento entregue, 0 depois do silêncio final, -EINVAL para
 *         argumento inválido ou iterador não inicializado. Com o buffer
 *         validado e inalterado, não há erro de conteúdo; @p out só muda
 *         quando o retorno é 1.
 */
int parser_rtttl_next(struct parser_rtttl *parser, struct playback_event *out);

/**
 * @brief Volta uma faixa validada ao primeiro evento.
 *
 * @return 0 no sucesso ou -EINVAL para argumento inválido.
 */
int parser_rtttl_rewind(struct parser_rtttl *parser);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_PARSER_RTTTL_H_ */

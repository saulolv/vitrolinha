/**
 * @file event.h
 * @brief Contrato: mudança de frequência em um instante da faixa.
 *
 * Tipo compartilhado pelo parser, loader e player. O instante é relativo
 * ao início da faixa; o player acrescentará sua base temporal ao agendar.
 * Frequência zero representa silêncio. A sequência termina com um evento
 * de silêncio no instante correspondente à duração total.
 *
 * Somente definição de interface; não há implementação neste arquivo.
 */

#ifndef VITROLINHA_EVENT_H_
#define VITROLINHA_EVENT_H_

#include <stdint.h>

/** @brief Frequência a aplicar em um instante relativo ao início da faixa. */
struct playback_event {
	/** @brief Instante em microssegundos, sem incluir pausas do usuário. */
	uint32_t offset_us;
	/** @brief Frequência em hertz; zero desliga o tom. */
	uint32_t freq_hz;
};

#endif /* VITROLINHA_EVENT_H_ */

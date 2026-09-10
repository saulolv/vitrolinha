/**
 * @file seqlock.h
 * @brief Publicação atômica de estrutura para leitores sem bloqueio.
 *
 * Primitivo de um escritor e vários leitores, para um único núcleo. Resolve
 * um problema concreto do Vitrolinha: o instantâneo de reprodução é escrito
 * do lado do player — inclusive **de dentro do callback do temporizador, em
 * contexto de interrupção** — e lido pela thread da interface a cada quadro.
 *
 * As duas metades são deliberadamente assimétricas:
 *
 * - **Escrita**: desabilita interrupções por algumas dezenas de ciclos. É o
 *   que impede que a thread `player` e o callback do temporizador escrevam ao
 *   mesmo tempo, já que ambos publicam.
 * - **Leitura**: nunca bloqueia e nunca desabilita interrupções. Se a
 *   interface travasse as interrupções para ler, o atraso entraria direto no
 *   desvio de início da nota, que o RNF02 limita a 5 ms.
 *
 * A leitura enxerga a estrutura inteira de uma versão só, ou repete. O padrão
 * de uso é sempre este laço:
 *
 * @code
 * uint32_t marca;
 *
 * do {
 *         marca = seqlock_read_begin(&sl);
 *         copia = compartilhado;
 * } while (seqlock_read_retry(&sl, marca));
 * @endcode
 *
 * @warning A cópia tem de estar **dentro** do laço. Fora dele, o mecanismo
 *          detecta a corrida e descarta o resultado certo, ficando com o
 *          errado.
 *
 * @note Vale para um núcleo só, que é o caso do RP2350 no Zephyr (não há
 *       porte SMP para Cortex-M). Em SMP, `irq_lock` não excluiria um
 *       escritor rodando no outro núcleo.
 */

#ifndef VITROLINHA_SEQLOCK_H_
#define VITROLINHA_SEQLOCK_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Contador de versão que serializa escritas e leituras.
 *
 * Ímpar significa escrita em curso; par, estrutura estável. O leitor descarta
 * a cópia se pegou o contador ímpar ou se ele mudou durante a cópia.
 */
struct seqlock {
	atomic_t seq;
};

/** @brief Valor inicial de um @ref seqlock em estado estável. */
#define SEQLOCK_INITIALIZER { .seq = ATOMIC_INIT(0) }

/**
 * @brief Abre a seção de escrita.
 *
 * Desabilita interrupções e marca a estrutura como instável. Chamável de
 * thread ou de contexto de interrupção.
 *
 * @param sl Trava. Não pode ser nula.
 *
 * @return Chave a devolver para @ref seqlock_write_unlock.
 */
unsigned int seqlock_write_lock(struct seqlock *sl);

/**
 * @brief Fecha a seção de escrita e reabilita interrupções.
 *
 * @param sl  Trava usada em @ref seqlock_write_lock.
 * @param key Chave devolvida por @ref seqlock_write_lock.
 */
void seqlock_write_unlock(struct seqlock *sl, unsigned int key);

/**
 * @brief Marca o início de uma leitura.
 *
 * Só amostra o contador; não espera o escritor terminar. Quem decide se a
 * cópia serve é @ref seqlock_read_retry — assim o laço de espera fica no
 * chamador, visível, em vez de escondido dentro do primitivo.
 *
 * @param sl Trava. Não pode ser nula.
 *
 * @return Marca a passar para @ref seqlock_read_retry.
 */
uint32_t seqlock_read_begin(const struct seqlock *sl);

/**
 * @brief Diz se a cópia feita depois de @ref seqlock_read_begin deve ser
 *        descartada.
 *
 * @param sl    Trava. Não pode ser nula.
 * @param begin Marca devolvida por @ref seqlock_read_begin.
 *
 * @retval true  A cópia pode estar inconsistente: repetir o laço.
 * @retval false A cópia corresponde a uma versão íntegra da estrutura.
 */
bool seqlock_read_retry(const struct seqlock *sl, uint32_t begin);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_SEQLOCK_H_ */

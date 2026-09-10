/**
 * @file seqlock.c
 * @brief Implementação de @ref seqlock.h.
 */

#include <vitrolinha/seqlock.h>

#include <zephyr/irq.h>
#include <zephyr/sys/atomic.h>

/*
 * As operações atômicas do Zephyr usam __ATOMIC_SEQ_CST, que é ao mesmo
 * tempo barreira de compilador e barreira de memória. É o que garante que a
 * cópia da estrutura não seja reordenada para fora da janela delimitada pelo
 * contador — sem isso o mecanismo inteiro não vale nada, porque o otimizador
 * teria liberdade para mover as escritas.
 */

unsigned int seqlock_write_lock(struct seqlock *sl)
{
	unsigned int key = irq_lock();

	/* Ímpar: leitores que pegarem esta versão descartam a cópia. */
	(void)atomic_inc(&sl->seq);

	return key;
}

void seqlock_write_unlock(struct seqlock *sl, unsigned int key)
{
	/* Par de novo, e diferente do valor anterior: quem começou a ler
	 * antes desta escrita também descarta.
	 */
	(void)atomic_inc(&sl->seq);

	irq_unlock(key);
}

uint32_t seqlock_read_begin(const struct seqlock *sl)
{
	return (uint32_t)atomic_get(&sl->seq);
}

bool seqlock_read_retry(const struct seqlock *sl, uint32_t begin)
{
	/* Ímpar em `begin`: a cópia começou no meio de uma escrita.
	 * Contador mudou: uma escrita terminou durante a cópia.
	 */
	return ((begin & 1U) != 0U) || (begin != (uint32_t)atomic_get(&sl->seq));
}

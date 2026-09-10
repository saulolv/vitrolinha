/**
 * @file snapshot.h
 * @brief Contrato: instantâneo de estado do player, lido pela interface.
 *
 * A barra de progresso precisa do tempo decorrido a cada quadro. Interrogar o
 * `player` por fila para isso o obrigaria a responder em ritmo de interface,
 * que é exatamente o acoplamento que a arquitetura evita.
 *
 * A saída é um **registro de escritor único**: só o lado do player escreve,
 * a `ui` lê sem bloqueio e calcula o decorrido por subtração. Esta é a
 * exceção deliberada à comunicação por filas. A regra do projeto não é
 * "nenhum estado compartilhado" — que seria incompatível com o próprio
 * requisito da barra de progresso — e sim **nenhum estado mutável
 * compartilhado sem disciplina de propriedade**.
 *
 * "Escritor único" quer dizer um único *lado*, não um único contexto: tanto a
 * thread `player` quanto o callback do temporizador publicam. A exclusão
 * entre os dois é do @ref seqlock, e é por isso que a publicação existe como
 * função em vez de atribuição direta ao registro.
 *
 * @see docs/adr/0001-instantaneo-de-estado.md
 * @see docs/especificacao-vitrolinha.md, seção 6.5
 */

#ifndef VITROLINHA_SNAPSHOT_H_
#define VITROLINHA_SNAPSHOT_H_

#include <stdint.h>

#include <vitrolinha/track.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Estado da máquina de reprodução. */
enum player_state {
	/** @brief Nada tocando. Inclui o fim de faixa, com o progresso em 100%. */
	PLAYER_STOPPED = 0,
	/** @brief Tocando. É o único estado em que o tempo decorrido avança. */
	PLAYER_PLAYING = 1,
	/** @brief Pausado. O decorrido fica congelado onde parou. */
	PLAYER_PAUSED = 2,
};

/**
 * @brief Fotografia do que o player está fazendo.
 *
 * @warning Nenhum campo deve ser lido de uma cópia obtida fora de
 *          @ref snapshot_read: os campos só são coerentes entre si quando
 *          copiados juntos.
 */
struct player_snapshot {
	/** @brief Um valor de @ref player_state. */
	uint32_t state;

	/**
	 * @brief Base temporal da faixa, em microssegundos. **Duas leituras.**
	 *
	 * - Em ::PLAYER_PLAYING: instante de `k_uptime` em que a faixa estava
	 *   na posição zero. O decorrido é `agora - base_us`, e o callback do
	 *   temporizador agenda cada evento em `base_us + offset_us`.
	 * - Em ::PLAYER_PAUSED e ::PLAYER_STOPPED: o próprio decorrido,
	 *   congelado. Sem isso o relógio da tela continuaria correndo com a
	 *   música parada.
	 *
	 * A dupla leitura é a razão de @ref snapshot_elapsed_us existir:
	 * nenhum chamador deve interpretar este campo por conta própria.
	 */
	uint64_t base_us;

	/** @brief Duração total da faixa, igual ao offset do último evento. */
	uint32_t total_us;

	/** @brief Índice na biblioteca, ou ::TRACK_NONE se nenhuma. */
	int16_t track;
};

/** @brief Valor de repouso: parado, no início, sem faixa. */
#define SNAPSHOT_IDLE_INITIALIZER                                              \
	{                                                                      \
		.state = PLAYER_STOPPED, .base_us = 0, .total_us = 0,          \
		.track = TRACK_NONE,                                           \
	}

/**
 * @brief Publica um novo instantâneo. **Só o lado do player chama.**
 *
 * Chamável da thread `player` e do callback do temporizador. Desabilita
 * interrupções pelo tempo de copiar a estrutura (dezenas de ciclos), o que é
 * o preço de a leitura nunca bloquear.
 *
 * @param in Instantâneo a publicar. Nulo é ignorado.
 */
void snapshot_publish(const struct player_snapshot *in);

/**
 * @brief Lê o instantâneo corrente, sem bloquear.
 *
 * Não desabilita interrupções e não espera em trava: pode repetir a cópia se
 * uma publicação acontecer no meio, o que custa alguns ciclos e nunca atrasa
 * a próxima nota.
 *
 * @param out Destino. Nulo é ignorado.
 */
void snapshot_read(struct player_snapshot *out);

/**
 * @brief Tempo decorrido da faixa, em microssegundos.
 *
 * Único lugar que sabe interpretar @ref player_snapshot::base_us. O
 * resultado nunca passa de @ref player_snapshot::total_us, para que a barra
 * de progresso não estoure se o relógio andar entre a publicação e a leitura.
 *
 * @param snap   Cópia obtida por @ref snapshot_read. Nulo devolve 0.
 * @param now_us Instante atual, na mesma base de @ref snapshot_publish
 *               (`k_uptime_ticks` convertido para microssegundos).
 *
 * @return Decorrido em microssegundos, entre 0 e `total_us`.
 */
uint32_t snapshot_elapsed_us(const struct player_snapshot *snap, uint64_t now_us);

/**
 * @brief Progresso da faixa em milésimos, pronto para a barra da tela.
 *
 * Milésimos e não porcentagem porque a barra tem 128 px: porcentagem inteira
 * faria o desenho saltar de 1,28 px em 1,28 px.
 *
 * @param snap   Cópia obtida por @ref snapshot_read. Nulo devolve 0.
 * @param now_us Instante atual, em microssegundos.
 *
 * @return Valor de 0 a 1000. Faixa de duração desconhecida devolve 0.
 */
uint16_t snapshot_progress_permille(const struct player_snapshot *snap, uint64_t now_us);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_SNAPSHOT_H_ */

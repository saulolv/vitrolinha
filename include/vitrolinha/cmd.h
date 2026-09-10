/**
 * @file cmd.h
 * @brief Contrato: comandos de transporte entre `input` e `player`.
 *
 * A regra que organiza o conjunto: **o transporte é global e independente da
 * tela exibida; o encoder é sempre navegação**. Nenhum comando carrega
 * informação de qual tela estava visível, e o `player` nunca precisa
 * perguntar.
 *
 * O produtor é a thread `input`; o consumidor é a thread `player`. A fila é
 * de tamanho fixo e a publicação nunca bloqueia, de modo que um `player`
 * ocupado atrase o comando mas jamais trave quem o produziu.
 *
 * @see docs/especificacao-vitrolinha.md, seção 8
 */

#ifndef VITROLINHA_CMD_H_
#define VITROLINHA_CMD_H_

#include <stdint.h>

#include <zephyr/kernel.h>

#include <vitrolinha/track.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Comandos de transporte. */
enum cmd_kind {
	/** @brief Botão físico 2: alterna entre tocar e pausar. */
	CMD_PLAY_PAUSE = 0,
	/** @brief Botão físico 3: faixa seguinte. */
	CMD_NEXT,
	/** @brief Botão físico 1: faixa anterior. */
	CMD_PREV,
	/**
	 * @brief Clique do encoder: toca a faixa indicada, agora.
	 *
	 * Substitui a faixa em curso. Não existe fila de reprodução:
	 * selecionar é tocar.
	 */
	CMD_SELECT,
	/** @brief Interrompe a reprodução e volta ao início. */
	CMD_STOP,

	/** @brief Sentinela; não é um comando. */
	CMD_KIND_COUNT,
};

/** @brief Um comando na fila. */
struct cmd {
	/** @brief Um valor de @ref cmd_kind. */
	uint8_t kind;
	/**
	 * @brief Faixa alvo.
	 *
	 * Só tem significado em ::CMD_SELECT. Nos demais comandos vale
	 * ::TRACK_NONE — quem decide a faixa é a máquina de estados do
	 * `player`, não quem apertou o botão.
	 */
	int16_t track;
};

/**
 * @brief Profundidade da fila de comandos.
 *
 * Oito cabe um dedo impaciente sem permitir que uma sequência longa de
 * comandos represados seja reproduzida depois, fora de contexto.
 */
#define CMD_QUEUE_DEPTH 8U

/**
 * @brief Enfileira um comando. Nunca bloqueia.
 *
 * Chamável de thread ou de contexto de interrupção, o que importa porque os
 * callbacks do subsistema `input` podem rodar em interrupção.
 *
 * @param kind  Comando a enviar.
 * @param track Faixa alvo. Obrigatória e não negativa em ::CMD_SELECT;
 *              ignorada nos demais, que gravam ::TRACK_NONE.
 *
 * @retval 0        Enfileirado.
 * @retval -EINVAL  @p kind desconhecido, ou ::CMD_SELECT com @p track
 *                  negativo ou além de ::LIBRARY_MAX.
 * @retval -ENOMSG  Fila cheia. O comando foi descartado.
 */
int cmd_post(enum cmd_kind kind, int16_t track);

/**
 * @brief Retira o próximo comando da fila.
 *
 * @param out     Destino. Não pode ser nulo.
 * @param timeout Quanto esperar. `K_NO_WAIT` para só espiar, `K_FOREVER`
 *                para dormir até chegar comando.
 *
 * @retval 0        Comando escrito em @p out.
 * @retval -EINVAL  @p out nulo.
 * @retval -EAGAIN  Prazo esgotado sem comando.
 * @retval -ENOMSG  Fila vazia e @p timeout igual a `K_NO_WAIT`.
 */
int cmd_get(struct cmd *out, k_timeout_t timeout);

/**
 * @brief Descarta todos os comandos pendentes.
 *
 * Usado ao trocar de faixa, para que botões apertados antes da troca não
 * ajam sobre a faixa nova, e no preparo de cada teste.
 */
void cmd_flush(void);

/**
 * @brief Nome legível de um comando, para log e para as mensagens de teste.
 *
 * @param kind Comando.
 *
 * @return Literal estático; `"?"` se @p kind não for um comando válido.
 */
const char *cmd_kind_name(enum cmd_kind kind);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_CMD_H_ */

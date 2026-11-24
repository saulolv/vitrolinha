/**
 * @file cmd.c
 * @brief Implementação de @ref cmd.h.
 */

#include <vitrolinha/cmd.h>

#include <errno.h>

#include <zephyr/kernel.h>

/**
 * @brief A fila.
 *
 * Privada: nenhum cabeçalho a declara, e nenhum módulo recebe a `k_msgq`.
 * Não há como alguém enfileirar um comando malformado por baixo da
 * validação, nem bloquear na publicação passando um timeout.
 *
 * Alinhamento 4 porque a estrutura tem um `int16_t` — a `k_msgq` copia por
 * blocos e exige o alinhamento natural do tipo.
 */
K_MSGQ_DEFINE(cmd_queue, sizeof(struct cmd), CMD_QUEUE_DEPTH, 4);

/**
 * @brief Nomes dos comandos, na ordem de @ref cmd_kind.
 *
 * Tabela em vez de `switch` para que acrescentar um comando sem lhe dar nome
 * quebre a verificação estática abaixo, em vez de passar batido e virar "?"
 * no log.
 */
static const char *const cmd_names[] = {
	[CMD_PLAY_PAUSE] = "PLAY_PAUSE",
	[CMD_NEXT] = "NEXT",
	[CMD_PREV] = "PREV",
	[CMD_SELECT] = "SELECT",
	[CMD_STOP] = "STOP",
	[CMD_CUE] = "CUE",
};

BUILD_ASSERT(ARRAY_SIZE(cmd_names) == (size_t)CMD_KIND_COUNT,
	     "cmd_names está fora de sincronia com enum cmd_kind");

int cmd_post(enum cmd_kind kind, int16_t track)
{
	struct cmd msg;

	if ((kind < CMD_PLAY_PAUSE) || (kind >= CMD_KIND_COUNT)) {
		return -EINVAL;
	}

	if ((kind == CMD_SELECT) || (kind == CMD_CUE)) {
		if ((track < 0) || (track >= (int16_t)LIBRARY_MAX)) {
			return -EINVAL;
		}
	} else {
		/* Normaliza: só ::CMD_SELECT e ::CMD_CUE carregam faixa. Deixar lixo aqui
		 * convidaria o consumidor a olhar o campo no comando errado.
		 */
		track = TRACK_NONE;
	}

	msg.kind = (uint8_t)kind;
	msg.track = track;

	/* Sempre K_NO_WAIT: isto roda em contexto de interrupção quando o
	 * callback do subsistema `input` publica direto.
	 */
	return k_msgq_put(&cmd_queue, &msg, K_NO_WAIT);
}

int cmd_get(struct cmd *out, k_timeout_t timeout)
{
	if (out == NULL) {
		return -EINVAL;
	}

	return k_msgq_get(&cmd_queue, out, timeout);
}

#ifdef CONFIG_POLL
void cmd_poll_event_init(struct k_poll_event *event)
{
	k_poll_event_init(event, K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY,
			  &cmd_queue);
}
#endif

void cmd_flush(void)
{
	k_msgq_purge(&cmd_queue);
}

const char *cmd_kind_name(enum cmd_kind kind)
{
	if ((kind < CMD_PLAY_PAUSE) || (kind >= CMD_KIND_COUNT)) {
		return "?";
	}

	return cmd_names[kind];
}

/**
 * @file main.c
 * @brief Testes de @ref cmd.h.
 *
 * A fila é o contrato entre `input` e `player`. O que se verifica aqui não é
 * só o vai-e-vem: é que a validação impeça a construção de comandos que o
 * consumidor teria de tratar como caso especial — ::CMD_SELECT sem faixa, ou
 * faixa pendurada num comando que não a usa.
 */

#include <vitrolinha/cmd.h>
#include <vitrolinha/track.h>

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/** @brief Cada caso começa com a fila vazia. */
static void cmd_before(void *fixture)
{
	ARG_UNUSED(fixture);
	cmd_flush();
}

ZTEST_SUITE(cmd, NULL, NULL, cmd_before, NULL, NULL);

/* -------------------------------------------------------------------------
 * Vai-e-vem
 * ------------------------------------------------------------------------- */

ZTEST(cmd, test_publica_e_consome)
{
	struct cmd got;

	zassert_ok(cmd_post(CMD_PLAY_PAUSE, TRACK_NONE));
	zassert_ok(cmd_get(&got, K_NO_WAIT));

	zassert_equal((uint8_t)CMD_PLAY_PAUSE, got.kind);
}

ZTEST(cmd, test_ordem_preservada)
{
	struct cmd got;

	zassert_ok(cmd_post(CMD_NEXT, TRACK_NONE));
	zassert_ok(cmd_post(CMD_PREV, TRACK_NONE));
	zassert_ok(cmd_post(CMD_STOP, TRACK_NONE));

	zassert_ok(cmd_get(&got, K_NO_WAIT));
	zassert_equal((uint8_t)CMD_NEXT, got.kind);
	zassert_ok(cmd_get(&got, K_NO_WAIT));
	zassert_equal((uint8_t)CMD_PREV, got.kind);
	zassert_ok(cmd_get(&got, K_NO_WAIT));
	zassert_equal((uint8_t)CMD_STOP, got.kind);
}

ZTEST(cmd, test_select_carrega_a_faixa)
{
	struct cmd got;

	zassert_ok(cmd_post(CMD_SELECT, 5));
	zassert_ok(cmd_get(&got, K_NO_WAIT));

	zassert_equal((uint8_t)CMD_SELECT, got.kind);
	zassert_equal(5, got.track);
}

ZTEST(cmd, test_faixa_e_normalizada_fora_do_select)
{
	struct cmd got;

	/* Quem aperta "seguinte" não escolhe faixa: quem escolhe é a máquina
	 * de estados. Deixar um índice aqui convidaria o consumidor a olhar
	 * um campo sem significado.
	 */
	zassert_ok(cmd_post(CMD_NEXT, 9));
	zassert_ok(cmd_get(&got, K_NO_WAIT));

	zassert_equal(TRACK_NONE, got.track);
}

/* -------------------------------------------------------------------------
 * Validação
 * ------------------------------------------------------------------------- */

ZTEST(cmd, test_comando_desconhecido_e_recusado)
{
	zassert_equal(-EINVAL, cmd_post(CMD_KIND_COUNT, TRACK_NONE));
	zassert_equal(-EINVAL, cmd_post((enum cmd_kind)-1, TRACK_NONE));
	zassert_equal(-EINVAL, cmd_post((enum cmd_kind)99, TRACK_NONE));
}

ZTEST(cmd, test_select_sem_faixa_e_recusado)
{
	zassert_equal(-EINVAL, cmd_post(CMD_SELECT, TRACK_NONE));
	zassert_equal(-EINVAL, cmd_post(CMD_SELECT, -7));
}

ZTEST(cmd, test_select_alem_da_biblioteca_e_recusado)
{
	zassert_equal(-EINVAL, cmd_post(CMD_SELECT, (int16_t)LIBRARY_MAX));
	zassert_equal(-EINVAL, cmd_post(CMD_SELECT, (int16_t)(LIBRARY_MAX + 1U)));
}

ZTEST(cmd, test_select_nos_extremos_da_biblioteca)
{
	struct cmd got;

	zassert_ok(cmd_post(CMD_SELECT, 0));
	zassert_ok(cmd_get(&got, K_NO_WAIT));
	zassert_equal(0, got.track);

	zassert_ok(cmd_post(CMD_SELECT, (int16_t)(LIBRARY_MAX - 1U)));
	zassert_ok(cmd_get(&got, K_NO_WAIT));
	zassert_equal((int16_t)(LIBRARY_MAX - 1U), got.track);
}

ZTEST(cmd, test_consumir_para_nulo_e_recusado)
{
	zassert_equal(-EINVAL, cmd_get(NULL, K_NO_WAIT));
}

/* -------------------------------------------------------------------------
 * Limites da fila
 * ------------------------------------------------------------------------- */

ZTEST(cmd, test_fila_cheia_descarta_sem_bloquear)
{
	for (unsigned int i = 0U; i < CMD_QUEUE_DEPTH; i++) {
		zassert_ok(cmd_post(CMD_STOP, TRACK_NONE),
			   "posicao %u deveria caber", i);
	}

	/* Não pode bloquear: isto roda em contexto de interrupção. */
	zassert_equal(-ENOMSG, cmd_post(CMD_STOP, TRACK_NONE));
}

ZTEST(cmd, test_fila_vazia_sem_espera)
{
	struct cmd got;

	zassert_equal(-ENOMSG, cmd_get(&got, K_NO_WAIT));
}

ZTEST(cmd, test_fila_vazia_com_prazo)
{
	struct cmd got;

	zassert_equal(-EAGAIN, cmd_get(&got, K_MSEC(5)));
}

ZTEST(cmd, test_descarte_esvazia)
{
	struct cmd got;

	zassert_ok(cmd_post(CMD_NEXT, TRACK_NONE));
	zassert_ok(cmd_post(CMD_PREV, TRACK_NONE));

	cmd_flush();

	zassert_equal(-ENOMSG, cmd_get(&got, K_NO_WAIT),
		      "comandos anteriores a uma troca de faixa nao podem sobreviver");
}

/* -------------------------------------------------------------------------
 * Nomes
 * ------------------------------------------------------------------------- */

ZTEST(cmd, test_todo_comando_tem_nome)
{
	static const char *const esperado[] = {
		[CMD_PLAY_PAUSE] = "PLAY_PAUSE",
		[CMD_NEXT] = "NEXT",
		[CMD_PREV] = "PREV",
		[CMD_SELECT] = "SELECT",
		[CMD_STOP] = "STOP",
	};

	for (int i = 0; i < (int)CMD_KIND_COUNT; i++) {
		zassert_str_equal(esperado[i], cmd_kind_name((enum cmd_kind)i),
				  "nome errado para o comando %d", i);
	}
}

ZTEST(cmd, test_nome_de_comando_invalido)
{
	zassert_str_equal("?", cmd_kind_name(CMD_KIND_COUNT));
	zassert_str_equal("?", cmd_kind_name((enum cmd_kind)-1));
}

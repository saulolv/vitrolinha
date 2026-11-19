/**
 * @file loader.h
 * @brief Contrato: carga assíncrona de faixa e fornecimento de eventos.
 *
 * O loader é dono do buffer de STORAGE_FILE_MAX bytes, do parser e de
 * uma fila de capacidade fixa. Sua thread faz E/S e validação; o player
 * apenas solicita trabalho e consome resultados sem bloquear.
 *
 * Cada solicitação recebe um identificador para rejeitar respostas e
 * eventos de faixas substituídas. O buffer só é reutilizado após
 * o produtor abandonar a solicitação anterior. Não há leitura do
 * armazenamento durante a reprodução de uma faixa já carregada.
 *
 * A fila tem 32 eventos. O worker repõe eventos da RAM à medida que o
 * player os consome. Pedidos usam um único slot substituível, sem espera.
 *
 * O player não precisa consultar o resultado em laço: o evento de
 * loader_poll_event_init() fica pronto quando o resultado de uma
 * solicitação fica disponível, e loader_get_result() o rearma.
 *
 * @note O formato sai do conteúdo, pela fachada @ref parser.h: RTTTL para as
 *       músicas, CSV para a bancada e a calibração.
 */

#ifndef VITROLINHA_LOADER_H_
#define VITROLINHA_LOADER_H_

#include <stddef.h>
#include <stdint.h>

#include <vitrolinha/event.h>

#ifdef __cplusplus
extern "C" {
#endif

struct k_poll_event;

/** @brief Resultado da preparação de uma faixa, consultado pelo player. */
struct loader_result {
	/** @brief Índice solicitado, conforme a biblioteca do storage. */
	int track;
	/** @brief Zero para faixa pronta; erro negativo para falha de carga. */
	int status;
	/** @brief Duração em microssegundos; válida quando status for zero. */
	uint32_t total_us;
	/** @brief Linha do arquivo que falhou, a partir de 1; zero se não aplicável. */
	size_t error_line;
	/** @brief Coluna do byte que falhou, a partir de 1; zero junto com a linha. */
	size_t error_column;
};

/**
 * @brief Prepara os recursos fixos e o processamento assíncrono do loader.
 * @return 0 no sucesso ou erro negativo de inicialização.
 * @note Chamar uma vez, em contexto de thread, antes do player.
 */
int loader_init(void);

/**
 * @brief Solicita a carga e validação de uma faixa sem realizar E/S aqui.
 * @param track Índice obtido na última varredura da biblioteca.
 * @param request_id Recebe um identificador não nulo se a solicitação for aceita.
 * @return 0 se aceita; -EINVAL para argumentos inválidos; -ENOMSG se não
 *         houver espaço para registrar a solicitação.
 * @note Em caso de sucesso, substitui a solicitação anterior. O player
 *       deve antes parar seu timer e silenciar; em caso de erro, a
 *       solicitação anterior permanece válida. Somente contexto de thread.
 */
int loader_request(int track, uint32_t *request_id);

/**
 * @brief Consulta a conclusão da validação e do abastecimento inicial da fila.
 * @param request_id Identificador da solicitação a consultar.
 * @param out Resultado de destino; consultar status quando retornar 0.
 * @return 0 para resultado disponível, -EAGAIN enquanto prepara, -ESTALE
 *         para solicitação substituída/cancelada ou -EINVAL para argumento inválido.
 * @note Não bloqueia. Resultado pronto permanece consultável até cancelamento
 *       ou substituição; somente então o player poderá iniciar o timer.
 *       Rearma o evento de loader_poll_event_init(): depois de -EAGAIN, o
 *       evento volta a ficar pronto quando o resultado chegar.
 */
int loader_get_result(uint32_t request_id, struct loader_result *out);

/**
 * @brief Prepara um evento de `k_poll` que fica pronto com um resultado novo.
 * @param event Evento a preparar. Não pode ser nulo.
 * @note O evento acorda; quem decide é loader_get_result(). Ele pode ficar
 *       pronto por uma solicitação já substituída, e a consulta devolve
 *       -EAGAIN ou -ESTALE nesse caso. Exige `CONFIG_POLL`.
 */
void loader_poll_event_init(struct k_poll_event *event);

/**
 * @brief Retira um evento já preparado da fila, sem E/S nem parsing.
 * @param request_id Identificador da faixa cujo resultado indicou sucesso.
 * @param out Evento de destino; válido apenas no retorno 1.
 * @return 1 para evento, 0 após consumir o silêncio final, -EAGAIN para
 *         fila temporariamente vazia, -ESTALE para solicitação inválida
 *         ou -EINVAL para argumento inválido.
 * @note Chamável do timer, sem espera. Fila vazia antes do fim exige
 *       tratamento de falha pelo player; não significa fim normal da faixa.
 */
int loader_next_event(uint32_t request_id, struct playback_event *out);

/**
 * @brief Solicita reinício da faixa em RAM, sem reler seu arquivo.
 * @param request_id Identificador da faixa já validada e ainda retida.
 * @param new_request_id Recebe a nova geração, a consultar por get_result.
 * @return 0 se aceita; -ESTALE para faixa indisponível; -EINVAL para
 *         argumentos inválidos; -ENOMSG se não puder enfileirar o pedido.
 * @note Em sucesso invalida request_id; em erro mantém a geração anterior.
 *       Chamar com timer parado. O loader reinicia o parser e repõe a fila.
 */
int loader_restart(uint32_t request_id, uint32_t *new_request_id);

/**
 * @brief Invalida uma solicitação e pede descarte seguro de seus recursos.
 * @param request_id Solicitação corrente, com o timer já parado.
 * @return 0 se invalidada ou -ESTALE se já não for a solicitação corrente.
 * @note Não espera E/S em andamento terminar; o worker libera o buffer
 *       quando for seguro. Chamar apenas em contexto de thread.
 */
int loader_cancel(uint32_t request_id);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_LOADER_H_ */

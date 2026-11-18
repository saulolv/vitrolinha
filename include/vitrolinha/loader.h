/**
 * @file loader.h
 * @brief Contrato: carga assíncrona de CSV e fornecimento de eventos.
 *
 * O loader é dono do buffer de STORAGE_FILE_MAX bytes e de um vetor fixo
 * com todos os eventos da faixa atual. Sua thread faz E/S e conversão; o player
 * apenas solicita trabalho e consome resultados sem bloquear.
 *
 * Cada solicitação recebe um identificador para rejeitar respostas e
 * eventos de faixas substituídas. O buffer só é reutilizado após
 * o produtor abandonar a solicitação anterior. Não há leitura do
 * armazenamento durante a reprodução de uma faixa já carregada.
 *
 * O vetor comporta qualquer CSV válido dentro de STORAGE_FILE_MAX. Durante
 * a reprodução não há parsing nem reposição. Pedidos usam um slot substituível.
 */

#ifndef VITROLINHA_LOADER_H_
#define VITROLINHA_LOADER_H_

#include <stddef.h>
#include <stdint.h>

#include <vitrolinha/event.h>

struct k_sem;

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Resultado da preparação de uma faixa, consultado pelo player. */
struct loader_result {
	/** @brief Índice solicitado, conforme a biblioteca do storage. */
	int track;
	/** @brief Zero para faixa pronta; erro negativo para falha de carga. */
	int status;
	/** @brief Duração em microssegundos; válida quando status for zero. */
	uint32_t total_us;
	/** @brief Linha do CSV que falhou, a partir de 1; zero se não aplicável. */
	size_t error_line;
};

/**
 * @brief Prepara os recursos fixos e o processamento assíncrono do loader.
 * @return 0 no sucesso ou erro negativo de inicialização.
 * @note Chamar uma vez, em contexto de thread, antes do player.
 */
int loader_init(void);

/**
 * @brief Registra o semáforo acordado ao concluir uma carga, inclusive com erro.
 * @note O semáforo pertence ao consumidor e deve permanecer válido. NULL
 *       desativa a notificação. O resultado é obtido por loader_get_result().
 */
void loader_set_wakeup(struct k_sem *wakeup);

/**
 * @brief Solicita a carga e validação de uma faixa sem realizar E/S aqui.
 * @param track Índice obtido na última varredura da biblioteca.
 * @param request_id Recebe um identificador não nulo se a solicitação for aceita.
 * @return 0 se aceita; -EINVAL para argumentos inválidos.
 * @note Em caso de sucesso, substitui a solicitação anterior. O player
 *       deve antes parar seu timer e silenciar; em caso de erro, a
 *       solicitação anterior permanece válida. Somente contexto de thread.
 */
int loader_request(int track, uint32_t *request_id);

/**
 * @brief Consulta a conclusão da conversão de todos os eventos da faixa.
 * @param request_id Identificador da solicitação a consultar.
 * @param out Resultado de destino; consultar status quando retornar 0.
 * @return 0 para resultado disponível, -EAGAIN enquanto prepara, -ESTALE
 *         para solicitação substituída/cancelada ou -EINVAL para argumento inválido.
 * @note Não bloqueia. Resultado pronto permanece consultável até cancelamento
 *       ou substituição; somente então o player poderá iniciar o timer.
 */
int loader_get_result(uint32_t request_id, struct loader_result *out);

/**
 * @brief Copia o próximo evento do vetor e avança o índice, sem E/S nem parsing.
 * @param request_id Identificador da faixa cujo resultado indicou sucesso.
 * @param out Evento de destino; válido apenas no retorno 1.
 * @return 1 para evento, 0 após consumir o silêncio final, -EAGAIN para
 *         faixa ainda não preparada, -ESTALE para solicitação inválida
 *         ou -EINVAL para argumento inválido.
 * @note Chamável do timer, sem espera. Após preparação bem-sucedida,
 *       todos os eventos estão disponíveis até substituição ou cancelamento.
 */
int loader_next_event(uint32_t request_id, struct playback_event *out);

/**
 * @brief Reinicia o índice da faixa em RAM, sem E/S nem parsing.
 * @param request_id Identificador da faixa já validada e ainda retida.
 * @param new_request_id Recebe a nova geração, já pronta para consumo.
 * @return 0 se aceita; -ESTALE para faixa indisponível; -EINVAL para
 *         argumentos inválidos.
 * @note Em sucesso invalida request_id; em erro mantém a geração anterior.
 *       Chamar com timer parado. Operação síncrona; não gera notificação.
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

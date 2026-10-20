/**
 * @file loader.c
 * @brief Esqueleto da preparação assíncrona de faixas para reprodução.
 *
 * Integrará storage.h e parser_csv.h em uma thread de baixa prioridade.
 * Carregará o arquivo inteiro, validará o CSV e abastecerá uma fila fixa
 * de eventos a partir da RAM. O consumo da fila não realizará parsing.
 *
 * A implementação deverá controlar a geração de cada solicitação,
 * invalidar trabalho cancelado e coordenar a reutilização do buffer.
 * Uma faixa concluída ficará retida até cancelamento ou substituição,
 * permitindo reinício sem nova leitura de arquivo.
 *
 * Somente assinaturas; nenhum worker, buffer ou fila foi implementado.
 * Arquivo ainda não integrado ao build.
 */

#include <vitrolinha/loader.h>

/** @brief A implementar: inicializar recursos e worker. @see loader.h */
int loader_init(void);

/** @brief A implementar: enfileirar uma carga. @see loader.h */
int loader_request(int track, uint32_t *request_id);

/** @brief A implementar: consultar a preparação. @see loader.h */
int loader_get_result(uint32_t request_id, struct loader_result *out);

/** @brief A implementar: consumir a fila sem espera. @see loader.h */
int loader_next_event(uint32_t request_id, struct playback_event *out);

/** @brief A implementar: reiniciar a faixa em RAM. @see loader.h */
int loader_restart(uint32_t request_id, uint32_t *new_request_id);

/** @brief A implementar: invalidar a solicitação. @see loader.h */
int loader_cancel(uint32_t request_id);

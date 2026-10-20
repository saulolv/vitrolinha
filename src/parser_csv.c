/**
 * @file parser_csv.c
 * @brief Esqueleto do parser de pares frequência/duração em CSV.
 *
 * Implementará o contrato parser_csv.h, validando o buffer integralmente
 * e oferecendo um iterador sem alocação. O evento de cada linha usará a
 * soma das durações anteriores; ao final haverá um evento de silêncio.
 *
 * Somente assinaturas para revisão. Não há funções implementadas e este
 * arquivo ainda não faz parte da lista de fontes do build.
 */

#include <vitrolinha/parser_csv.h>

/**
 * @brief A implementar: validar cabeçalho, campos, limites e duração total.
 * @see parser_csv.h para parâmetros, retornos e limites do contrato.
 */
int parser_csv_init(struct parser_csv *parser, const uint8_t *buffer,
		    size_t length, struct parser_csv_error *error);

/**
 * @brief A implementar: entregar a próxima mudança de tom ou o silêncio final.
 * @see parser_csv.h para parâmetros e distinção entre evento e fim.
 */
int parser_csv_next(struct parser_csv *parser, struct playback_event *out);

/**
 * @brief A implementar: reiniciar a leitura do buffer já validado.
 * @see parser_csv.h para a validade do buffer e os códigos de retorno.
 */
int parser_csv_rewind(struct parser_csv *parser);

/**
 * @file parser_csv.h
 * @brief Contrato: validação e leitura de eventos de um CSV em memória.
 *
 * Formato: cabeçalho obrigatório frequency_hz,duration_ms,
 * seguido de dois inteiros decimais por linha. Frequência zero é silêncio;
 * duração deve ser positiva. Aceita LF, CRLF e última linha sem quebra.
 * Linhas vazias, espaços, sinais, frações, campos entre aspas e colunas
 * extras são rejeitados. Um CR isolado não é uma quebra de linha válida.
 *
 * O parser preserva as frequências e durações, sem transposição nem
 * articulação automática. Não faz E/S, alocação dinâmica ou acesso ao
 * relógio. Todo o arquivo é validado antes de disponibilizar eventos.
 * Frequência diferente de zero fora de ::TONE_FREQ_MIN_HZ .. ::TONE_FREQ_MAX_HZ
 * é recusada já na validação. A soma das durações não pode exceder
 * 4294967 ms.
 */

#ifndef VITROLINHA_PARSER_CSV_H_
#define VITROLINHA_PARSER_CSV_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <vitrolinha/event.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A primeira linha de todo CSV de faixa, sem o fim de linha.
 *
 * Uma constante só para o parser, a fachada que reconhece o formato e a
 * varredura do cartão: conferir o cabeçalho com regra mais frouxa em um deles
 * listaria como válida uma faixa que a carga recusaria.
 */
#define PARSER_CSV_HEADER "frequency_hz,duration_ms"

/** @brief Localização de um erro de validação. */
struct parser_csv_error {
	/** @brief Linha a partir de 1; zero para erro sem linha associada. */
	size_t line;
	/** @brief Coluna a partir de 1; zero quando não aplicável. */
	size_t column;
};

/**
 * @brief Estado de um iterador, armazenado pelo chamador sem alocação.
 *
 * O buffer pertence ao chamador e deve permanecer válido e inalterado
 * enquanto o iterador for utilizado. Após init, total_us e row_count podem
 * ser consultados; os demais campos são de uso interno do parser.
 */
struct parser_csv {
	const uint8_t *buffer;
	size_t length;
	size_t position;
	size_t line;
	uint32_t offset_us;
	/** @brief Duração total validada, em microssegundos. */
	uint32_t total_us;
	/** @brief Número de linhas musicais, sem contar o cabeçalho. */
	size_t row_count;
	bool final_event_emitted;
};

/**
 * @brief Valida a faixa inteira e prepara o iterador no primeiro evento.
 * @param parser Estado de destino; zerado em caso de falha.
 * @param buffer CSV completo; não precisa terminar em NUL.
 * @param length Quantidade exata de bytes disponíveis em buffer.
 * @param error Local do erro; pode ser NULL. Zerado em caso de sucesso.
 * @return 0 no sucesso; -EINVAL para argumento/formato inválido; -ERANGE
 *         para valores ou soma das durações que excedam os tipos do contrato,
 *         e para frequência diferente de zero fora de ::TONE_FREQ_MIN_HZ ..
 *         ::TONE_FREQ_MAX_HZ, apontada na coluna 1 da linha.
 * @note Exigir ao menos uma linha musical e verificar a soma em 64 bits
 *       antes de converter para total_us. Não chamar no callback do timer.
 */
int parser_csv_init(struct parser_csv *parser, const uint8_t *buffer,
		    size_t length, struct parser_csv_error *error);

/**
 * @brief Obtém o próximo evento, incluindo o silêncio final da faixa.
 * @param parser Iterador inicializado com sucesso.
 * @param out Evento de destino; só é válido quando o retorno for 1.
 * @return 1 para evento entregue, 0 após o evento final ou erro negativo
 *         (-EINVAL para argumento/formato inválido; -ERANGE para overflow).
 *         O fim não é confundido com freq_hz zero; out não muda sem evento.
 * @note Cada linha produz um evento no início de sua duração. Executar
 *       no loader; o callback do timer receberá eventos já preparados.
 *       Com um buffer validado e inalterado, não haverá erro de conteúdo.
 */
int parser_csv_next(struct parser_csv *parser, struct playback_event *out);

/**
 * @brief Reposiciona uma faixa validada no primeiro evento.
 * @param parser Iterador inicializado cujo buffer permanece inalterado.
 * @return 0 no sucesso ou -EINVAL para argumento inválido.
 */
int parser_csv_rewind(struct parser_csv *parser);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_PARSER_CSV_H_ */

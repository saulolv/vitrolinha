/**
 * @file track.h
 * @brief Contrato: metadados de uma faixa da biblioteca.
 *
 * Cabeçalho compartilhado pelas três trilhas de desenvolvimento. É o
 * vocabulário comum entre quem varre o cartão (`storage`), quem desenha a
 * lista (`ui`) e quem toca (`player`).
 *
 * O consumo de RAM da biblioteca é fixo por construção: LIBRARY_MAX faixas de
 * TRACK_NAME_MAX bytes, 768 B, independentemente de quantos arquivos existam
 * no cartão. É assim que o RNF06 é satisfeito sem depender de medição.
 *
 * @see docs/especificacao-vitrolinha.md, RNF06
 */

#ifndef VITROLINHA_TRACK_H_
#define VITROLINHA_TRACK_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tamanho do campo de nome, em bytes, incluindo o terminador.
 *
 * A tela tem 128 px de largura e a fonte UNSCII-8 é de largura fixa 8 px,
 * o que dá uma grade exata de 16 colunas. Os 24 bytes cobrem esses 16
 * caracteres com folga para o terminador e para eventual acento em UTF-8.
 */
#define TRACK_NAME_MAX 24U

/**
 * @brief Número máximo de faixas na biblioteca.
 *
 * Arquivos além do 32.º são ignorados com aviso na tela. O teto é o que
 * mantém o consumo de RAM e o custo de heap do `lv_list` fixos.
 */
#define LIBRARY_MAX 32U

/** @brief Índice que representa "nenhuma faixa". */
#define TRACK_NONE (-1)

/**
 * @brief Metadados de uma faixa da biblioteca.
 *
 * Não guarda o conteúdo do arquivo nem o nome no sistema de arquivos: só o
 * necessário para desenhar a lista e para pedir a faixa ao `storage`.
 */
struct track_meta {
	/**
	 * @brief Nome exibido, sempre terminado em NUL.
	 *
	 * Vem do **cabeçalho RTTTL**, não do nome do arquivo: sem
	 * `CONFIG_FS_FATFS_LFN` a leitura de diretório devolve 8.3 em
	 * maiúsculas (`FURELIS.TXT`), ruim numa tela cujo único texto é este.
	 */
	char name[TRACK_NAME_MAX];

	/** @brief Posição na biblioteca, de 0 a LIBRARY_MAX-1. */
	uint8_t index;

	/**
	 * @brief Falso se o interpretador rejeitou o arquivo.
	 *
	 * A faixa continua listada, marcada com `!`, em vez de sumir sem
	 * explicação.
	 */
	bool valid;
};

/**
 * @brief Copia um nome para um campo de tamanho fixo, truncando se preciso.
 *
 * Existe porque a origem do nome quase nunca é uma string C: o cabeçalho
 * RTTTL termina em `:`, não em NUL, e a entrada de diretório do FatFs tem
 * limite próprio. Concentrar a regra aqui evita repetir a mesma aritmética de
 * truncamento em cada chamador — e cada repetição dessas é uma chance de
 * esquecer o terminador.
 *
 * @param dst      Destino. Recebe sempre uma string terminada em NUL.
 * @param dst_size Tamanho de @p dst em bytes, terminador incluído.
 * @param src      Origem. Não precisa ser terminada em NUL.
 * @param src_len  Quantos bytes de @p src considerar.
 *
 * @return Número de bytes escritos em @p dst, sem contar o terminador. Menor
 *         que @p src_len indica truncamento.
 *
 * @note Seguro com @p dst_size igual a zero (não escreve nada e devolve 0) e
 *       com @p src nulo (produz string vazia).
 */
size_t track_name_copy(char *dst, size_t dst_size, const char *src, size_t src_len);

/**
 * @brief Preenche um @ref track_meta, garantindo suas invariantes.
 *
 * Zera a estrutura inteira antes de escrever, para que faixas distintas com
 * nomes de comprimentos distintos não deixem bytes residuais no campo de
 * nome — o que tornaria duas entradas iguais comparáveis como diferentes.
 *
 * @param meta  Estrutura a preencher. Nulo é ignorado.
 * @param index Posição na biblioteca.
 * @param name  Nome de origem; pode não ser terminado em NUL. Nulo vira "".
 * @param name_len Quantos bytes de @p name considerar.
 * @param valid Se o interpretador aceitou o arquivo.
 */
void track_meta_init(struct track_meta *meta, uint8_t index, const char *name,
		     size_t name_len, bool valid);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_TRACK_H_ */

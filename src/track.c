/**
 * @file track.c
 * @brief Implementação de @ref track.h.
 */

#include <vitrolinha/track.h>

#include <string.h>

/**
 * @brief Quantos bytes de continuação um caractere UTF-8 pode ter.
 *
 * A sequência mais longa tem quatro bytes: o líder e três continuações. Recuar
 * mais do que isso já não é procurar o começo de um caractere, é atravessar
 * lixo — e lixo não justifica apagar o nome inteiro.
 */
#define UTF8_CONT_MAX 3U

/**
 * @brief Diz se @p byte é continuação de uma sequência UTF-8 (`10xxxxxx`).
 */
static bool utf8_is_continuation(char byte)
{
	return ((uint8_t)byte & 0xC0U) == 0x80U;
}

/**
 * @brief Recua um corte até a fronteira de caractere mais próxima.
 *
 * O corte fica antes de `src[cut]`. Se esse byte é continuação, o caractere a
 * que ele pertence começou antes do corte e ficaria partido; recuar até o
 * byte líder deixa o caractere inteiro de fora.
 *
 * @param src Origem, com pelo menos `cut + 1` bytes válidos.
 * @param cut Posição do corte.
 * @return A posição do corte, já numa fronteira.
 */
static size_t utf8_boundary(const char *src, size_t cut)
{
	size_t back = 0U;

	while ((back < UTF8_CONT_MAX) && (back < cut) &&
	       utf8_is_continuation(src[cut - back])) {
		back++;
	}

	return cut - back;
}

size_t track_name_copy(char *dst, size_t dst_size, const char *src, size_t src_len)
{
	size_t copied;

	if ((dst == NULL) || (dst_size == 0U)) {
		return 0U;
	}

	if (src == NULL) {
		src_len = 0U;
	}

	/* Uma posição fica sempre reservada para o terminador: é o que torna
	 * impossível devolver daqui uma string não terminada, independentemente
	 * do que o chamador passe.
	 */
	copied = (src_len < (dst_size - 1U)) ? src_len : (dst_size - 1U);

	if (copied < src_len) {
		copied = utf8_boundary(src, copied);
	}

	if (copied > 0U) {
		(void)memcpy(dst, src, copied);
	}
	dst[copied] = '\0';

	return copied;
}

void track_meta_init(struct track_meta *meta, uint8_t index, const char *name,
		     size_t name_len, bool valid)
{
	if (meta == NULL) {
		return;
	}

	/* Zerar antes de escrever: sem isso, reaproveitar uma entrada com nome
	 * curto sobre outra de nome longo deixaria bytes da anterior depois do
	 * terminador, e duas faixas de mesmo nome deixariam de ser iguais em
	 * comparação byte a byte.
	 */
	(void)memset(meta, 0, sizeof(*meta));

	(void)track_name_copy(meta->name, sizeof(meta->name), name, name_len);
	meta->index = index;
	meta->valid = valid;
}

/** @brief Espaço ou tabulação: o que se apara em volta do nome. */
static bool is_blank(char c)
{
	return (c == ' ') || (c == '\t');
}

/** @brief Fim de linha. Não pode aparecer dentro do nome. */
static bool is_eol(char c)
{
	return (c == '\r') || (c == '\n');
}

bool track_rtttl_name(const char *buf, size_t len, const char **name, size_t *name_len)
{
	static const char bom[] = "\xEF\xBB\xBF";
	size_t start = 0U;
	size_t end;

	if ((len >= (sizeof(bom) - 1U)) && (memcmp(buf, bom, sizeof(bom) - 1U) == 0)) {
		start = sizeof(bom) - 1U;
	}

	while ((start < len) && (is_blank(buf[start]) || is_eol(buf[start]))) {
		start++;
	}

	for (end = start; (end < len) && (buf[end] != ':'); end++) {
		if (is_eol(buf[end])) {
			return false;
		}
	}

	if (end == len) {
		return false;
	}

	while ((end > start) && is_blank(buf[end - 1U])) {
		end--;
	}

	*name = &buf[start];
	*name_len = end - start;

	return true;
}

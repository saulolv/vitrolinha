/**
 * @file track.c
 * @brief Implementação de @ref track.h.
 */

#include <vitrolinha/track.h>

#include <string.h>

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

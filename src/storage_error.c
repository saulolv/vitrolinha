/**
 * @file storage_error.c
 * @brief As mensagens de tela dos erros do @ref storage.h.
 *
 * Fora de `storage.c` e de `storage_fake.c` porque o texto é do contrato, não
 * de quem o implementa: as duas implementações devolvem os mesmos códigos, e
 * a tela não pode dizer coisas diferentes no simulador e na placa.
 */

#include <vitrolinha/storage.h>

#include <zephyr/toolchain.h>

#include <errno.h>

/*
 * A mensagem do arquivo grande diz o teto por extenso. Se o buffer de faixa
 * mudar, ela tem de mudar junto — e é aqui que a compilação avisa.
 */
BUILD_ASSERT(STORAGE_FILE_MAX == 4096U, "atualize a mensagem de -EFBIG");

const char *storage_error_text(int err)
{
	switch (err) {
	case -ENODEV:
		return "Sem cartao";
	case -EIO:
		return "Erro de leitura";
	case -EFBIG:
		return "Maior que 4 KB";
	case -ENOENT:
		return "Faixa sumiu";
	case -EINVAL:
		return "Pedido invalido";
	default:
		return "Erro inesperado";
	}
}

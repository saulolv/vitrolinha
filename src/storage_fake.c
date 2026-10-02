/**
 * @file storage_fake.c
 * @brief Implementação de mentira de @ref storage.h, com faixas embutidas.
 *
 * Serve o contrato inteiro sem cartão, sem sistema de arquivos e sem SPI.
 * Existe para destravar as trilhas A e B antes de o cartão existir e para
 * tornar reproduzíveis os caminhos de falha, que com hardware de verdade
 * dependem de alguém puxar o cartão na hora certa.
 *
 * Na placa quem responde é o storage do cartão (`storage.c`), que se apoia
 * no módulo `card`; aqui fica o simulador, que não tem disco. A escolha é do
 * `CONFIG_VITROLINHA_STORAGE`, e nenhum chamador sabe qual das duas está no
 * binário — é o que a fronteira do @ref storage.h compra.
 */

#include <vitrolinha/storage.h>
#include <vitrolinha/storage_fake.h>
#include <vitrolinha/track.h>

#include <errno.h>
#include <string.h>

/**
 * @brief Uma faixa embutida no binário.
 *
 * O nome fica ao lado do conteúdo em vez de ser extraído do cabeçalho: a regra
 * de leitura do cabeçalho mora em `storage.c`, e repeti-la aqui seria manter
 * duas cópias dela.
 */
struct fake_track {
	const char *name;
	const char *content;
};

/*
 * Melodias reais, em RTTTL, escolhidas por serem curtas e por cobrirem os
 * casos que o interpretador vai precisar tratar: pausa (`p`), duração
 * pontuada (`e.`) e duração por nota (`8d`).
 */
static const struct fake_track fake_tracks[STORAGE_FAKE_TRACK_COUNT] = {
	{
		.name = "Fur Elise",
		.content = "Fur Elise:d=8,o=5,b=125:"
			   "e6,d#6,e6,d#6,e6,b,d6,c6,a,p,c,e,a,b,p,e,g#,b,c6,"
			   "p,e,e6,d#6,e6,d#6,e6,b,d6,c6,a,p,c,e,a,b,p,e,c6,b,a",
	},
	{
		.name = "Ode to Joy",
		.content = "Ode to Joy:d=4,o=5,b=125:"
			   "e,e,f,g,g,f,e,d,c,c,d,e,e.,8d,2d,"
			   "e,e,f,g,g,f,e,d,c,c,d,e,d.,8c,2c",
	},
};

/** @brief Cartão presente? Manipulado por @ref storage_fake_set_present. */
static bool fake_present = true;

/** @brief Cartão respondendo? Manipulado por @ref storage_fake_set_failing. */
static bool fake_failing;

/**
 * @brief Traduz o estado simulado do cartão em código de erro.
 *
 * Concentra num só lugar a ordem em que as falhas são testadas — "sem
 * cartão" antes de "cartão mudo" —, para que @ref storage_scan e
 * @ref storage_load não possam divergir uma da outra.
 *
 * @return 0 se o cartão está utilizável, ou o erro negativo correspondente.
 */
static int fake_card_status(void)
{
	if (!fake_present) {
		return -ENODEV;
	}

	if (fake_failing) {
		return -EIO;
	}

	return 0;
}

void storage_fake_set_present(bool present)
{
	fake_present = present;
}

void storage_fake_set_failing(bool failing)
{
	fake_failing = failing;
}

void storage_fake_reset(void)
{
	fake_present = true;
	fake_failing = false;
}

int storage_scan(struct track_meta *out, size_t max, size_t *skipped)
{
	int status;
	size_t count;

	if (skipped != NULL) {
		*skipped = 0U;
	}

	if ((out == NULL) || (max == 0U)) {
		return -EINVAL;
	}

	status = fake_card_status();
	if (status != 0) {
		return status;
	}

	/* O teto da biblioteca vale mesmo que o chamador ofereça mais espaço:
	 * é ele que mantém o consumo de RAM previsível (RNF06).
	 */
	if (max > LIBRARY_MAX) {
		max = LIBRARY_MAX;
	}

	count = (max < STORAGE_FAKE_TRACK_COUNT) ? max : STORAGE_FAKE_TRACK_COUNT;

	for (size_t i = 0U; i < count; i++) {
		track_meta_init(&out[i], (uint8_t)i, fake_tracks[i].name,
				strlen(fake_tracks[i].name), true);
	}

	if (skipped != NULL) {
		*skipped = STORAGE_FAKE_TRACK_COUNT - count;
	}

	return (int)count;
}

int storage_load(int track, uint8_t *buf, size_t len)
{
	int status;
	size_t size;

	if ((buf == NULL) || (len == 0U)) {
		return -EINVAL;
	}

	status = fake_card_status();
	if (status != 0) {
		return status;
	}

	if ((track < 0) || (track >= (int)STORAGE_FAKE_TRACK_COUNT)) {
		return -ENOENT;
	}

	size = strlen(fake_tracks[track].content);

	/* Rejeitar, não truncar: meia melodia sem aviso é pior do que um erro
	 * na tela.
	 */
	if (size > len) {
		return -EFBIG;
	}

	(void)memcpy(buf, fake_tracks[track].content, size);

	return (int)size;
}

bool storage_present(void)
{
	return fake_present;
}

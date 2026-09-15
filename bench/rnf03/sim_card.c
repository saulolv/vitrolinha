/**
 * @file sim_card.c
 * @brief O cartão de mentira: disco esparso, contador de setores e relógio.
 *
 * Três coisas num arquivo só, porque são a mesma: o disco esparso existe para
 * poder formatar 8 GiB sem 8 GiB de memória, o contador existe porque é o que
 * a medição quer, e a cobrança de tempo existe para que `k_cycle_get_64()`
 * devolva no simulador um número da mesma natureza do que a placa vai
 * devolver.
 *
 * @see sim_card.h para o porquê da geometria, sd_cost.h para o modelo de tempo
 */

#include "bench.h"
#include "sd_cost.h"
#include "sim_card.h"

#include <zephyr/drivers/disk.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/util.h>

#include <ff.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(sim_card, LOG_LEVEL_INF);

/** @brief Setor de 512 B, como todo cartão SDHC. */
#define SECTOR_SIZE 512U

/** @brief Capacidade anunciada: 8 GiB, em setores. */
#define SECTOR_COUNT (8U * 1024U * 2048U)

/**
 * @brief Tamanho do aglomerado.
 *
 * 32 KiB é o que o formatador oficial do SD escolhe para cartões de 4 a
 * 32 GB. Importa mais do que parece: com aglomerado grande o diretório raiz
 * inteiro cabe num aglomerado só, e abrir um arquivo não precisa consultar a
 * FAT nenhuma vez.
 */
#define CLUSTER_BYTES 32768U

/**
 * @brief Páginas de 512 B disponíveis.
 *
 * Formatar 8 GiB em FAT32 com duas FATs escreve cerca de 4200 setores: as
 * duas tabelas, a área reservada e o aglomerado da raiz. As 32 faixas somam
 * menos de 150. O resto é folga para que estourar seja erro de programação,
 * não de dimensionamento.
 */
#define MAX_PAGES 12288U

/** @brief Posições na tabela de dispersão. Potência de dois, com folga. */
#define SLOT_COUNT 32768U

BUILD_ASSERT((SLOT_COUNT & (SLOT_COUNT - 1U)) == 0U, "SLOT_COUNT deve ser potencia de dois");
BUILD_ASSERT(MAX_PAGES < SLOT_COUNT, "a tabela precisa de folga para sondagem linear");

/** @brief Dados das páginas materializadas. */
static uint8_t pages[MAX_PAGES][SECTOR_SIZE];

/** @brief Índice da página em cada posição, deslocado de um. Zero é vazio. */
static uint32_t slots[SLOT_COUNT];

/** @brief Número do setor guardado em cada posição ocupada. */
static uint32_t slot_sector[SLOT_COUNT];

/** @brief Páginas já entregues. */
static uint32_t pages_used;

/** @brief Trilha da janela de medição corrente. */
static struct sim_card_trace trace;

/** @brief Cobrança de tempo ligada? */
static bool charging;

/** @brief Tempo de acesso do cartão por bloco. */
static uint32_t poll_ns = SD_COST_DEFAULT_POLL_NS;

/**
 * @brief Resto de nanossegundo ainda não cobrado.
 *
 * O relógio do `native_sim` anda em microssegundos inteiros, e o custo de um
 * setor não é inteiro em microssegundos. Guardar o resto em vez de arredondar
 * cada cobrança evita o mesmo erro acumulativo que o CLAUDE.md descreve na
 * grade temporal das notas — aqui numa escala em que ele também apareceria:
 * são milhares de cobranças por medição.
 */
static uint64_t debt_ns;

/**
 * @brief Posição de @p sector na tabela, ocupada ou livre.
 *
 * Dispersão multiplicativa de Knuth mais sondagem linear. A tabela nunca
 * enche — ::MAX_PAGES é menor que ::SLOT_COUNT —, então o laço sempre para.
 */
static uint32_t slot_of(uint32_t sector)
{
	uint32_t i = (sector * 2654435761U) & (SLOT_COUNT - 1U);

	while (slots[i] != 0U && slot_sector[i] != sector) {
		i = (i + 1U) & (SLOT_COUNT - 1U);
	}

	return i;
}

/**
 * @brief Página de @p sector, ou NULL se nunca foi escrita.
 */
static uint8_t *page_find(uint32_t sector)
{
	uint32_t i = slot_of(sector);

	return (slots[i] == 0U) ? NULL : pages[slots[i] - 1U];
}

/**
 * @brief Página de @p sector, materializando-a zerada se preciso.
 *
 * @return A página, ou NULL se as páginas acabaram.
 */
static uint8_t *page_get(uint32_t sector)
{
	uint32_t i = slot_of(sector);

	if (slots[i] != 0U) {
		return pages[slots[i] - 1U];
	}

	if (pages_used >= MAX_PAGES) {
		LOG_ERR("Acabaram as paginas do cartao de mentira (%u)", MAX_PAGES);
		return NULL;
	}

	(void)memset(pages[pages_used], 0, SECTOR_SIZE);
	slot_sector[i] = sector;
	slots[i] = ++pages_used;

	return pages[pages_used - 1U];
}

/**
 * @brief Faz o relógio simulado andar @p ns nanossegundos.
 *
 * `arch_busy_wait` é o único jeito de fazer tempo passar no `native_sim`: o
 * simulador trata a execução de código como instantânea e só avança o relógio
 * quando a CPU é parada de propósito.
 *
 * @see zephyr/boards/native/native_sim/cpu_wait.c
 */
static void charge(uint64_t ns)
{
	uint32_t us;

	if (!charging) {
		return;
	}

	debt_ns += ns;
	us = (uint32_t)(debt_ns / 1000U);
	debt_ns -= (uint64_t)us * 1000U;

	if (us != 0U) {
		k_busy_wait(us);
	}
}

/**
 * @brief Anota uma chamada de leitura na trilha.
 */
static void trace_read(uint32_t start, uint32_t count)
{
	if (trace.calls < SIM_CARD_TRACE_MAX) {
		trace.blocks[trace.calls] = count;
		trace.start[trace.calls] = start;
	} else {
		trace.dropped++;
	}

	trace.calls++;
	trace.sectors += count;
}

static int sim_disk_status(struct disk_info *disk)
{
	ARG_UNUSED(disk);

	return DISK_STATUS_OK;
}

static int sim_disk_read(struct disk_info *disk, uint8_t *buf, uint32_t start, uint32_t count)
{
	ARG_UNUSED(disk);

	if ((uint64_t)start + count > SECTOR_COUNT) {
		return -EIO;
	}

	trace_read(start, count);

	for (uint32_t i = 0U; i < count; i++) {
		const uint8_t *page = page_find(start + i);

		/* Setor nunca escrito lê como zero, que é o que um cartão
		 * recém-formatado devolve fora das áreas que o mkfs tocou.
		 */
		if (page == NULL) {
			(void)memset(&buf[i * SECTOR_SIZE], 0, SECTOR_SIZE);
		} else {
			(void)memcpy(&buf[i * SECTOR_SIZE], page, SECTOR_SIZE);
		}
	}

	charge(sd_cost_read_ns(count, poll_ns));

	return 0;
}

static int sim_disk_write(struct disk_info *disk, const uint8_t *buf, uint32_t start,
			  uint32_t count)
{
	ARG_UNUSED(disk);

	if ((uint64_t)start + count > SECTOR_COUNT) {
		return -EIO;
	}

	for (uint32_t i = 0U; i < count; i++) {
		uint8_t *page = page_get(start + i);

		if (page == NULL) {
			return -ENOSPC;
		}

		(void)memcpy(page, &buf[i * SECTOR_SIZE], SECTOR_SIZE);
	}

	/* A escrita não é cobrada: a aplicação nunca escreve no cartão, e o
	 * que este disco escreve é só a formatação e a biblioteca de mentira.
	 */
	return 0;
}

static int sim_disk_ioctl(struct disk_info *disk, uint8_t cmd, void *buf)
{
	ARG_UNUSED(disk);

	switch (cmd) {
	case DISK_IOCTL_GET_SECTOR_COUNT:
		*(uint32_t *)buf = SECTOR_COUNT;
		break;
	case DISK_IOCTL_GET_SECTOR_SIZE:
		*(uint32_t *)buf = SECTOR_SIZE;
		break;
	case DISK_IOCTL_GET_ERASE_BLOCK_SZ:
		*(uint32_t *)buf = 1U;
		break;
	case DISK_IOCTL_CTRL_SYNC:
	case DISK_IOCTL_CTRL_INIT:
	case DISK_IOCTL_CTRL_DEINIT:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sim_disk_init(struct disk_info *disk)
{
	return sim_disk_ioctl(disk, DISK_IOCTL_CTRL_INIT, NULL);
}

static const struct disk_operations sim_disk_ops = {
	.init = sim_disk_init,
	.status = sim_disk_status,
	.read = sim_disk_read,
	.write = sim_disk_write,
	.ioctl = sim_disk_ioctl,
};

static struct disk_info sim_disk = {
	.name = SIM_CARD_DISK_NAME,
	.ops = &sim_disk_ops,
};

/**
 * @brief Escreve uma faixa RTTTL de @p size bytes exatos.
 *
 * O conteúdo é melodia de verdade — cabeçalho com andamento e oitava, notas
 * separadas por vírgula —, repetida até o tamanho pedido. O que a medição
 * quer do arquivo é o tamanho; o que a fidelidade quer é que ele seja
 * interpretável quando o interpretador existir (issue #10).
 */
static int write_track(uint32_t index, uint32_t size)
{
	static const char *const notes[] = {"8e6", "8d#6", "8e6", "8b",
					    "8d6", "8c6",  "4a",  "8p"};
	char path[40];
	struct fs_file_t file;
	uint32_t written = 0U;
	int err;

	(void)snprintf(path, sizeof(path), BENCH_MOUNT_POINT "/TRK%02u.TXT", index);

	fs_file_t_init(&file);

	err = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE);
	if (err != 0) {
		LOG_ERR("fs_open(%s): %d", path, err);
		return err;
	}

	while (written < size) {
		char chunk[40];
		int len;

		if (written == 0U) {
			len = snprintf(chunk, sizeof(chunk), "Faixa %02u:d=8,o=5,b=125:", index);
		} else {
			len = snprintf(chunk, sizeof(chunk), "%s,",
				       notes[(written / 4U) % ARRAY_SIZE(notes)]);
		}

		/* A última escrita é aparada para o arquivo ficar com o
		 * tamanho pedido na régua, e não com o tamanho que o último
		 * pedaço calhou de ter.
		 */
		if (written + (uint32_t)len > size) {
			len = (int)(size - written);
		}

		err = fs_write(&file, chunk, len);
		if (err < 0) {
			LOG_ERR("fs_write(%s): %d", path, err);
			(void)fs_close(&file);
			return err;
		}

		written += (uint32_t)err;
	}

	return fs_close(&file);
}

int sim_card_setup(uint32_t tracks)
{
	static FATFS fat;
	static struct fs_mount_t mp = {
		.type = FS_FATFS,
		.fs_data = &fat,
		.mnt_point = BENCH_MOUNT_POINT,
	};
	/* Sem FM_SFD: um cartão de verdade tem tabela de partição, e onde a
	 * área de dados começa muda quais setores o FatFs lê.
	 */
	MKFS_PARM parm = {
		.fmt = FM_FAT32,
		.n_fat = 2,
		.align = 1,
		.n_root = 0,
		.au_size = CLUSTER_BYTES,
	};
	int err;

	err = disk_access_register(&sim_disk);
	if (err != 0) {
		LOG_ERR("disk_access_register: %d", err);
		return err;
	}

	/* O `dev_id` do FatFs é a string do volume, sem a barra inicial que o
	 * subsistema exige no ponto de montagem.
	 */
	err = fs_mkfs(FS_FATFS, (uintptr_t)(BENCH_MOUNT_POINT + 1), &parm, 0);
	if (err != 0) {
		LOG_ERR("fs_mkfs: %d", err);
		return err;
	}

	err = fs_mount(&mp);
	if (err != 0) {
		LOG_ERR("fs_mount: %d", err);
		return err;
	}

	for (uint32_t i = 0U; i < tracks; i++) {
		err = write_track(i, (i + 1U) * BENCH_TRACK_STEP);
		if (err != 0) {
			return err;
		}
	}

	LOG_INF("Cartao simulado: %u faixas, %u setores materializados (%u KiB)", tracks,
		pages_used, pages_used * SECTOR_SIZE / 1024U);

	/* Desmontar de propósito: a primeira medição é a da montagem fria. */
	return fs_unmount(&mp);
}

void sim_card_arm(bool on)
{
	charging = on;
	debt_ns = 0U;
}

void sim_card_set_poll_ns(uint32_t ns)
{
	poll_ns = ns;
}

void sim_card_trace_reset(void)
{
	trace = (struct sim_card_trace){0};
}

void sim_card_trace_get(struct sim_card_trace *out)
{
	*out = trace;
}

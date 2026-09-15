/**
 * @file fake_disk.c
 * @brief O cartão de mentira: 1 MiB em RAM com soquete e falhas comandáveis.
 *
 * @see fake_disk.h para o porquê, e para a máquina de estados que ele imita
 */

#include "fake_disk.h"

#include <vitrolinha/card.h>

#include <zephyr/drivers/disk.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/disk_access.h>

#include <errno.h>
#include <string.h>

/** @brief Setor de 512 B, como todo cartão SDHC. */
#define SECTOR_SIZE 512U

/**
 * @brief Capacidade: 1 MiB.
 *
 * Grande o bastante para o `f_mkfs` produzir um volume válido, pequeno o
 * bastante para caber num vetor estático. A geometria não importa aqui, ao
 * contrário da bancada do RNF03: este teste não conta setores, só quer um
 * volume que monte.
 */
#define SECTOR_COUNT 2048U

/** @brief O conteúdo do cartão. Sobrevive a tirar e pôr. */
static uint8_t sectors[SECTOR_COUNT][SECTOR_SIZE];

/** @brief O disco está ligado ao `disk_access`? */
static bool registered;

/** @brief Há cartão no soquete? */
static bool inserted;

/** @brief O cartão já passou pelo CTRL_INIT? */
static bool initialized;

static bool failing_init;
static bool failing_read;
static bool failing_deinit;

static int fake_init(struct disk_info *disk)
{
	ARG_UNUSED(disk);

	if (!inserted) {
		return DISK_STATUS_NOMEDIA;
	}

	if (failing_init) {
		return -EIO;
	}

	initialized = true;

	return 0;
}

static int fake_status(struct disk_info *disk)
{
	ARG_UNUSED(disk);

	if (!inserted) {
		return DISK_STATUS_NOMEDIA;
	}

	return initialized ? DISK_STATUS_OK : DISK_STATUS_UNINIT;
}

static int fake_read(struct disk_info *disk, uint8_t *buf, uint32_t start, uint32_t count)
{
	ARG_UNUSED(disk);

	if (!inserted || failing_read) {
		return -EIO;
	}

	if ((uint64_t)start + count > SECTOR_COUNT) {
		return -EIO;
	}

	(void)memcpy(buf, sectors[start], (size_t)count * SECTOR_SIZE);

	return 0;
}

static int fake_write(struct disk_info *disk, const uint8_t *buf, uint32_t start, uint32_t count)
{
	ARG_UNUSED(disk);

	if (!inserted) {
		return -EIO;
	}

	if ((uint64_t)start + count > SECTOR_COUNT) {
		return -EIO;
	}

	(void)memcpy(sectors[start], buf, (size_t)count * SECTOR_SIZE);

	return 0;
}

static int fake_ioctl(struct disk_info *disk, uint8_t cmd, void *buf)
{
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
		break;
	case DISK_IOCTL_CTRL_INIT:
		return fake_init(disk);
	case DISK_IOCTL_CTRL_DEINIT:
		initialized = false;
		return failing_deinit ? -EIO : 0;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct disk_operations fake_ops = {
	.init = fake_init,
	.status = fake_status,
	.read = fake_read,
	.write = fake_write,
	.ioctl = fake_ioctl,
};

static struct disk_info fake_disk = {
	/* O mesmo nome do `disk-name` do nó `sdhc0`, que é o que o módulo sob
	 * teste deriva de CARD_MOUNT_POINT.
	 */
	.name = CARD_MOUNT_POINT + 1,
	.ops = &fake_ops,
};

int fake_disk_setup(void)
{
	bool force = true;
	int err;

	fake_disk_register(true);

	/* Formatar exige cartão no soquete. O `dev_id` do FatFs é a string do
	 * volume, sem a barra inicial que o subsistema de arquivos exige no
	 * ponto de montagem.
	 */
	inserted = true;

	err = fs_mkfs(FS_FATFS, (uintptr_t)(CARD_MOUNT_POINT + 1), NULL, 0);
	if (err != 0) {
		return err;
	}

	/* A formatação inicializou o disco e ninguém a desfez. Zerar a
	 * contagem de referências aqui é o que faz cada teste começar do mesmo
	 * lugar, em vez de herdar um `+1` do preparo.
	 */
	(void)disk_access_ioctl(CARD_MOUNT_POINT + 1, DISK_IOCTL_CTRL_DEINIT, &force);

	fake_disk_reset();

	return 0;
}

void fake_disk_register(bool present)
{
	if (present == registered) {
		return;
	}

	if (present) {
		(void)disk_access_register(&fake_disk);
	} else {
		(void)disk_access_unregister(&fake_disk);
	}

	registered = present;
}

void fake_disk_insert(bool present)
{
	inserted = present;
}

void fake_disk_fail_init(bool failing)
{
	failing_init = failing;
}

void fake_disk_fail_read(bool failing)
{
	failing_read = failing;
}

void fake_disk_fail_deinit(bool failing)
{
	failing_deinit = failing;
}

void fake_disk_reset(void)
{
	fake_disk_register(true);

	inserted = false;
	failing_init = false;
	failing_read = false;
	failing_deinit = false;
}

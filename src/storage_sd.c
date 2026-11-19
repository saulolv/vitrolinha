/**
 * @file storage_sd.c
 * @brief Catálogo de CSVs na raiz de /SD: e leitura da faixa selecionada.
 *
 * card_init() deve montar o cartão antes da varredura. Guarda somente nomes
 * em RAM; o conteúdo é lido por storage_load(), no contexto do loader.
 * Após trocar o cartão, executar nova varredura antes de usar seus índices.
 * Nomes seguem a configuração do FatFs (8.3 quando LFN está desligado).
 */
#include <vitrolinha/card.h>
#include <vitrolinha/storage.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/fs/fs.h>

static char filenames[LIBRARY_MAX][MAX_FILE_NAME + 1];
static size_t track_count;

int storage_scan(struct track_meta *out, size_t max)
{
	if (out == NULL || max == 0) {
		return -EINVAL;
	}
	track_count = 0;
	if (!card_ready()) {
		return card_get_state() == CARD_ABSENT ? -ENODEV : -EIO;
	}
	struct fs_dir_t dir;
	struct fs_dirent entry;

	fs_dir_t_init(&dir);
	if (fs_opendir(&dir, CARD_MOUNT_POINT) != 0) {
		return -EIO;
	}
	size_t capacity = max < LIBRARY_MAX ? max : LIBRARY_MAX;
	int err;

	while ((err = fs_readdir(&dir, &entry)) == 0 && entry.name[0] != '\0') {
		size_t length = strlen(entry.name);

		if (entry.type != FS_DIR_ENTRY_FILE || length < 4 ||
		    entry.name[length - 4] != '.' ||
		    tolower((unsigned char)entry.name[length - 3]) != 'c' ||
		    tolower((unsigned char)entry.name[length - 2]) != 's' ||
		    tolower((unsigned char)entry.name[length - 1]) != 'v') {
			continue;
		}
		/* Conserva os primeiros nomes em ordem, como storage_host. */
		size_t pos = 0;

		while (pos < track_count && strcmp(filenames[pos], entry.name) < 0) {
			pos++;
		}
		if (pos == capacity) {
			continue;
		}
		if (track_count < capacity) {
			track_count++;
		}
		memmove(&filenames[pos + 1], &filenames[pos],
			(track_count - pos - 1) * sizeof(filenames[0]));
		strcpy(filenames[pos], entry.name);
	}
	int close_err = fs_closedir(&dir);

	if (err != 0 || close_err != 0) {
		track_count = 0;
		return -EIO;
	}
	for (size_t i = 0; i < track_count; i++) {
		/* Candidato: o parser validará o conteúdo quando a faixa for carregada. */
		track_meta_init(&out[i], i, filenames[i], strlen(filenames[i]) - 4, true);
	}
	return (int)track_count;
}

int storage_load(int track, uint8_t *buf, size_t len)
{
	if (buf == NULL || len == 0) {
		return -EINVAL;
	}
	if (!card_ready()) {
		return card_get_state() == CARD_ABSENT ? -ENODEV : -EIO;
	}
	if (track < 0 || (size_t)track >= track_count) {
		return -ENOENT;
	}
	char path[sizeof(CARD_MOUNT_POINT) + 1 + MAX_FILE_NAME];
	struct fs_dirent entry;

	snprintf(path, sizeof(path), "%s/%s", CARD_MOUNT_POINT, filenames[track]);
	int err = fs_stat(path, &entry);

	if (err != 0) {
		return err == -ENOENT ? -ENOENT : -EIO;
	}
	if (entry.type != FS_DIR_ENTRY_FILE) {
		return -EIO;
	}
	if (entry.size > STORAGE_FILE_MAX || entry.size > len) {
		return -EFBIG;
	}
	struct fs_file_t file;

	fs_file_t_init(&file);
	err = fs_open(&file, path, FS_O_READ);
	if (err != 0) {
		return err == -ENOENT ? -ENOENT : -EIO;
	}
	size_t offset = 0;

	while (offset < entry.size) {
		ssize_t bytes = fs_read(&file, buf + offset, entry.size - offset);

		if (bytes <= 0) {
			(void)fs_close(&file);
			return -EIO;
		}
		offset += bytes;
	}
	return fs_close(&file) == 0 ? (int)offset : -EIO;
}

bool storage_present(void)
{
	return card_ready();
}

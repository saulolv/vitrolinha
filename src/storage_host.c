/**
 * @file storage_host.c
 * @brief Biblioteca de faixas em uma pasta do computador, para native_sim.
 *
 * A varredura seleciona VITROLINHA_MUSIC_DIR ou fixtures/music, relativo ao
 * diretório de execução. Pasta e índices ficam mantidos até a próxima
 * varredura. Uso exclusivo do contexto de armazenamento, fora do player.
 * Faixas são os RTTTL (.txt, .rtx, .rtt) e os CSV (.csv), sem distinguir
 * maiúsculas. O nome exibido do RTTTL vem do cabeçalho, pela mesma regra do
 * cartão (@ref track_rtttl_name), e o RTTTL sem cabeçalho entra inválido; o
 * CSV fica com o nome do arquivo, como no cartão. Apenas esta implementação
 * ou storage_fake deve entrar no build.
 */

#define _POSIX_C_SOURCE 200809L

#include <vitrolinha/storage.h>
#include <vitrolinha/track.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zephyr/sys/util.h>

static char directory[PATH_MAX];
static char filenames[LIBRARY_MAX][NAME_MAX + 1];
static size_t track_count;

/** @brief Extensões de faixa: as quatro têm três letras. */
static const char *const extensions[] = {".txt", ".rtx", ".rtt", ".csv"};

/** @brief O nome termina numa extensão de faixa? */
static bool is_track_name(const char *name, size_t length)
{
	if (length < 4U) {
		return false;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(extensions); i++) {
		if (strcasecmp(name + length - 4U, extensions[i]) == 0) {
			return true;
		}
	}

	return false;
}

/**
 * @brief Preenche os metadados da faixa @p i, já na lista ordenada.
 *
 * O RTTTL é lido inteiro, pela própria carga, para achar o nome no
 * cabeçalho: os arquivos têm poucos KB, e reaproveitar a carga mantém um só
 * caminho de leitura. O que a carga recusa, ou que não tem cabeçalho, entra
 * inválido com o nome do arquivo.
 */
static void describe_track(size_t i, struct track_meta *meta)
{
	static uint8_t content[STORAGE_FILE_MAX];
	const char *name = filenames[i];
	size_t name_len = strlen(name) - 4U;
	bool valid = true;

	if (strcasecmp(name + name_len, ".csv") != 0) {
		int size = storage_load((int)i, content, sizeof(content));
		const char *header;
		size_t header_len;

		valid = (size > 0) &&
			track_rtttl_name((const char *)content, (size_t)size, &header, &header_len);
		if (valid && (header_len > 0U)) {
			name = header;
			name_len = header_len;
		}
	}

	track_meta_init(meta, (uint8_t)i, name, name_len, valid);
}

/** @brief Origem configurada para a próxima varredura. */
static const char *configured_directory(void)
{
	const char *path = getenv("VITROLINHA_MUSIC_DIR");

	return path != NULL ? path : "fixtures/music";
}

/** @brief Lista as primeiras faixas regulares por nome, sem visitar subpastas. */
int storage_scan(struct track_meta *out, size_t max, size_t *skipped)
{
	if (skipped != NULL) {
		*skipped = 0U;
	}
	if (out == NULL || max == 0U) {
		return -EINVAL;
	}

	track_count = 0U;
	const char *path = configured_directory();

	if (strlen(path) >= sizeof(directory)) {
		directory[0] = '\0';
		return -EIO;
	}
	strcpy(directory, path);
	DIR *dir = opendir(directory);

	if (dir == NULL) {
		return errno == ENOENT || errno == ENOTDIR ? -ENODEV : -EIO;
	}

	size_t capacity = max < LIBRARY_MAX ? max : LIBRARY_MAX;
	size_t found = 0U;
	int result = 0;

	for (;;) {
		errno = 0;
		struct dirent *entry = readdir(dir);

		if (entry == NULL) {
			result = errno == 0 ? 0 : -EIO;
			break;
		}
		size_t length = strlen(entry->d_name);

		if (!is_track_name(entry->d_name, length)) {
			continue;
		}
		struct stat info;

		if (fstatat(dirfd(dir), entry->d_name, &info, AT_SYMLINK_NOFOLLOW) < 0) {
			result = -EIO;
			break;
		}
		if (!S_ISREG(info.st_mode)) {
			continue;
		}
		found++;

		/* Inserção limitada: a seleção independe da ordem de readdir. */
		size_t pos = 0U;

		while (pos < track_count && strcmp(filenames[pos], entry->d_name) < 0) {
			pos++;
		}
		if (pos == capacity) {
			continue;
		}
		if (track_count < capacity) {
			track_count++;
		}
		memmove(&filenames[pos + 1U], &filenames[pos],
			(track_count - pos - 1U) * sizeof(filenames[0]));
		strcpy(filenames[pos], entry->d_name);
	}
	closedir(dir);
	if (result < 0) {
		track_count = 0U;
		return result;
	}
	if (skipped != NULL) {
		*skipped = found - track_count;
	}
	for (size_t i = 0; i < track_count; i++) {
		describe_track(i, &out[i]);
	}
	return (int)track_count;
}

/** @brief Lê a faixa inteira, rejeitando tamanho excessivo antes de escrever. */
int storage_load(int track, uint8_t *buf, size_t len)
{
	if (buf == NULL || len == 0U) {
		return -EINVAL;
	}
	if (track < 0 || (size_t)track >= track_count) {
		return -ENOENT;
	}
	DIR *dir = opendir(directory);

	if (dir == NULL) {
		return errno == ENOENT || errno == ENOTDIR ? -ENODEV : -EIO;
	}
	int fd = openat(dirfd(dir), filenames[track], O_RDONLY);
	int result = errno == ENOENT ? -ENOENT : -EIO;

	closedir(dir);
	if (fd < 0) {
		return result;
	}
	struct stat info;

	result = -EIO;
	if (fstat(fd, &info) < 0 || !S_ISREG(info.st_mode)) {
		goto done;
	}
	if (info.st_size > STORAGE_FILE_MAX || (uintmax_t)info.st_size > len) {
		result = -EFBIG;
		goto done;
	}

	size_t size = (size_t)info.st_size;
	size_t offset = 0U;

	while (offset < size) {
		ssize_t bytes = read(fd, buf + offset, size - offset);

		if (bytes < 0 && errno == EINTR) {
			continue;
		}
		if (bytes <= 0) {
			goto done;
		}
		offset += bytes;
	}
	result = (int)size;
done:
	close(fd);
	return result;
}

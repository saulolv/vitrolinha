/** @file main.c
 * @brief Testes do storage_host com arquivos temporários reais do computador.
 */

#define _POSIX_C_SOURCE 200809L

#include <vitrolinha/storage.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zephyr/ztest.h>

static char directory[64];
static int directory_fd;
static struct track_meta tracks[LIBRARY_MAX + 1U];
static const char csv[] = "frequency_hz,duration_ms\n440,200\n0,100\n";

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	strcpy(directory, "/tmp/vitrolinha-storage-XXXXXX");
	zassert_not_null(mkdtemp(directory));
	directory_fd = open(directory, O_RDONLY | O_DIRECTORY);
	zassert_true(directory_fd >= 0);
	zassert_ok(setenv("VITROLINHA_MUSIC_DIR", directory, 1));
	zassert_equal(0, storage_scan(tracks, ARRAY_SIZE(tracks), NULL));
}

static void after(void *fixture)
{
	ARG_UNUSED(fixture);
	DIR *dir = fdopendir(directory_fd);
	struct dirent *entry;

	zassert_not_null(dir);
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		struct stat info;

		zassert_ok(fstatat(directory_fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW));
		zassert_ok(unlinkat(directory_fd, entry->d_name,
				   S_ISDIR(info.st_mode) ? AT_REMOVEDIR : 0));
	}
	zassert_ok(closedir(dir));
	zassert_ok(rmdir(directory));
	zassert_ok(unsetenv("VITROLINHA_MUSIC_DIR"));
}

static void write_file(const char *name, const void *data, size_t size)
{
	int fd = openat(directory_fd, name, O_WRONLY | O_CREAT | O_TRUNC, 0600);

	zassert_true(fd >= 0);
	zassert_equal(size, write(fd, data, size));
	zassert_ok(close(fd));
}

ZTEST_SUITE(storage_host, NULL, NULL, before, after, NULL);

ZTEST(storage_host, test_listagem_e_leitura_csv)
{
	uint8_t buffer[sizeof(csv)];

	write_file("zeta.csv", "0,1", 3);
	write_file("beta.CsV", csv, sizeof(csv) - 1U);
	write_file("alfa.csv", "440,1", 5);
	write_file("ignorar.txt", csv, sizeof(csv) - 1U);
	zassert_ok(mkdirat(directory_fd, "subpasta.csv", 0700));
	zassert_ok(symlinkat("alfa.csv", directory_fd, "atalho.csv"));
	zassert_equal(1, storage_scan(tracks, 1, NULL));
	zassert_str_equal("alfa", tracks[0].name);
	zassert_equal(3, storage_scan(tracks, ARRAY_SIZE(tracks), NULL));
	zassert_str_equal("alfa", tracks[0].name);
	zassert_str_equal("beta", tracks[1].name);
	zassert_str_equal("zeta", tracks[2].name);
	for (size_t i = 0; i < 3U; i++) {
		zassert_equal(i, tracks[i].index);
		zassert_true(tracks[i].valid);
	}

	/* Mudanças de origem e arquivos novos só alteram índices após nova varredura. */
	zassert_ok(setenv("VITROLINHA_MUSIC_DIR", "/dev/null/ausente", 1));
	write_file("aaa.csv", csv, sizeof(csv) - 1U);
	zassert_true(storage_present());
	memset(buffer, 0xa5, sizeof(buffer));
	zassert_equal(sizeof(csv) - 1U, storage_load(1, buffer, sizeof(buffer)));
	zassert_mem_equal(csv, buffer, sizeof(csv) - 1U);
	zassert_equal(0xa5, buffer[sizeof(csv) - 1U]);
}

ZTEST(storage_host, test_limites_de_biblioteca_e_buffer)
{
	static uint8_t data[STORAGE_FILE_MAX + 1U];
	static uint8_t buffer[sizeof(data)];

	memset(data, 0x5a, sizeof(data));
	write_file("a.csv", data, STORAGE_FILE_MAX);
	write_file("b.csv", data, sizeof(data));
	for (int i = LIBRARY_MAX; i >= 0; i--) {
		char name[16];

		snprintf(name, sizeof(name), "c%02d.csv", i);
		write_file(name, csv, sizeof(csv) - 1U);
	}
	zassert_equal(LIBRARY_MAX, storage_scan(tracks, ARRAY_SIZE(tracks), NULL));
	zassert_str_equal("a", tracks[0].name);
	zassert_str_equal("c29", tracks[LIBRARY_MAX - 1U].name);
	zassert_equal(STORAGE_FILE_MAX, storage_load(0, buffer, STORAGE_FILE_MAX));
	zassert_mem_equal(data, buffer, STORAGE_FILE_MAX);

	memset(buffer, 0xa5, sizeof(buffer));
	zassert_equal(-EFBIG, storage_load(0, buffer, STORAGE_FILE_MAX - 1U));
	zassert_equal(-EFBIG, storage_load(1, buffer, sizeof(buffer)));
	for (size_t i = 0; i < sizeof(buffer); i++) {
		zassert_equal(0xa5, buffer[i]);
	}
	zassert_equal(-ENOENT, storage_load(LIBRARY_MAX, buffer, sizeof(buffer)));
}

ZTEST(storage_host, test_pasta_vazia_e_ausente)
{
	uint8_t buffer[sizeof(csv)];
	char missing[sizeof(directory) + 16U];

	zassert_true(storage_present());
	zassert_equal(0, storage_scan(tracks, ARRAY_SIZE(tracks), NULL));
	write_file("teste.csv", csv, sizeof(csv) - 1U);
	zassert_equal(1, storage_scan(tracks, ARRAY_SIZE(tracks), NULL));
	zassert_ok(unlinkat(directory_fd, "teste.csv", 0));
	zassert_equal(-ENOENT, storage_load(0, buffer, sizeof(buffer)));

	snprintf(missing, sizeof(missing), "%s/ausente", directory);
	zassert_ok(setenv("VITROLINHA_MUSIC_DIR", missing, 1));
	zassert_equal(-ENODEV, storage_scan(tracks, ARRAY_SIZE(tracks), NULL));
	zassert_false(storage_present());
	zassert_equal(-ENOENT, storage_load(0, buffer, sizeof(buffer)));
}

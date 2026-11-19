/** @file main.c
 * @brief Testes do storage_host com arquivos temporários reais do computador.
 */

#define _POSIX_C_SOURCE 200809L

#include <vitrolinha/storage.h>

#include "host_libc_fault.h"

#include <dirent.h>
#include <limits.h>
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
	host_libc_fault_arm(HOST_LIBC_FAULT_NONE);
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
	write_file("ignorar.mp3", csv, sizeof(csv) - 1U);
	write_file("abc", csv, sizeof(csv) - 1U);
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

ZTEST(storage_host, test_listagem_rtttl_com_nome_do_cabecalho)
{
	static uint8_t big[STORAGE_FILE_MAX + 1U];
	static const char rtttl[] = "Fur Elise:d=8,o=5,b=125:e6,d#6,e6";
	static const char anonymous[] = ":d=4:c";
	static const char headless[] = "e6,d#6,e6";

	memset(big, 'c', sizeof(big));
	big[0] = 'G';
	big[1] = ':';
	big[2] = ':';

	write_file("a.rtx", rtttl, sizeof(rtttl) - 1U);
	write_file("b.RTT", rtttl, sizeof(rtttl) - 1U);
	write_file("c.txt", anonymous, sizeof(anonymous) - 1U);
	write_file("d.txt", headless, sizeof(headless) - 1U);
	write_file("e.rtx", "", 0U);
	write_file("f.rtx", big, sizeof(big));
	write_file("g.csv", csv, sizeof(csv) - 1U);

	zassert_equal(7, storage_scan(tracks, ARRAY_SIZE(tracks), NULL));

	/* O nome da tela sai do cabeçalho, como no cartão. */
	zassert_str_equal("Fur Elise", tracks[0].name);
	zassert_true(tracks[0].valid);
	zassert_str_equal("Fur Elise", tracks[1].name, "extensao em maiusculas");
	zassert_true(tracks[1].valid);

	/* Cabeçalho sem nome: fica o do arquivo, e a faixa vale. */
	zassert_str_equal("c", tracks[2].name);
	zassert_true(tracks[2].valid);

	/* Sem cabeçalho, vazio, ou maior que o buffer: listado e inválido, com
	 * o nome do arquivo.
	 */
	zassert_str_equal("d", tracks[3].name);
	zassert_false(tracks[3].valid);
	zassert_str_equal("e", tracks[4].name);
	zassert_false(tracks[4].valid);
	zassert_str_equal("f", tracks[5].name);
	zassert_false(tracks[5].valid);

	/* O CSV segue com o nome do arquivo. */
	zassert_str_equal("g", tracks[6].name);
	zassert_true(tracks[6].valid);
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
	size_t skipped = 99U;

	/* a, b e c00..c32: 35 candidatos, 32 lugares. */
	zassert_equal(LIBRARY_MAX, storage_scan(tracks, ARRAY_SIZE(tracks), &skipped));
	zassert_equal(3U, skipped);
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

ZTEST(storage_host, test_argumentos_invalidos)
{
	uint8_t buffer[sizeof(csv)];
	size_t skipped = 99U;

	zassert_equal(-EINVAL, storage_scan(NULL, 1, &skipped));
	zassert_equal(0U, skipped);
	zassert_equal(-EINVAL, storage_scan(tracks, 0, NULL));
	zassert_equal(-EINVAL, storage_load(0, NULL, sizeof(buffer)));
	zassert_equal(-EINVAL, storage_load(0, buffer, 0));
	zassert_equal(-ENOENT, storage_load(-1, buffer, sizeof(buffer)));
}

ZTEST(storage_host, test_origem_padrao_relativa_ao_diretorio_de_execucao)
{
	static char previous[PATH_MAX];

	zassert_not_null(getcwd(previous, sizeof(previous)));
	zassert_ok(mkdirat(directory_fd, "fixtures", 0700));
	zassert_ok(mkdirat(directory_fd, "fixtures/music", 0700));
	write_file("fixtures/music/padrao.csv", csv, sizeof(csv) - 1U);
	zassert_ok(unsetenv("VITROLINHA_MUSIC_DIR"));
	zassert_ok(chdir(directory));

	int found = storage_scan(tracks, ARRAY_SIZE(tracks), NULL);

	zassert_ok(chdir(previous));
	zassert_equal(1, found);
	zassert_str_equal("padrao", tracks[0].name);
	zassert_ok(unlinkat(directory_fd, "fixtures/music/padrao.csv", 0));
	zassert_ok(unlinkat(directory_fd, "fixtures/music", AT_REMOVEDIR));
}

ZTEST(storage_host, test_origem_inutilizavel)
{
	static char path[PATH_MAX + 8U];

	/* Caminho que não cabe: recusado antes de abrir qualquer coisa, e sem
	 * pasta guardada a presença volta a consultar a origem configurada.
	 */
	memset(path, 'a', sizeof(path) - 1U);
	path[0] = '/';
	path[sizeof(path) - 1U] = '\0';
	zassert_ok(setenv("VITROLINHA_MUSIC_DIR", path, 1));
	zassert_equal(-EIO, storage_scan(tracks, ARRAY_SIZE(tracks), NULL));
	zassert_false(storage_present());

	/* Um arquivo no lugar da pasta é ausência, não falha de E/S. */
	write_file("arquivo.csv", csv, sizeof(csv) - 1U);
	snprintf(path, sizeof(path), "%s/arquivo.csv", directory);
	zassert_ok(setenv("VITROLINHA_MUSIC_DIR", path, 1));
	zassert_equal(-ENODEV, storage_scan(tracks, ARRAY_SIZE(tracks), NULL));

	/* Um laço de links é falha de E/S. */
	zassert_ok(symlinkat("laco", directory_fd, "laco"));
	snprintf(path, sizeof(path), "%s/laco", directory);
	zassert_ok(setenv("VITROLINHA_MUSIC_DIR", path, 1));
	zassert_equal(-EIO, storage_scan(tracks, ARRAY_SIZE(tracks), NULL));
}

ZTEST(storage_host, test_falha_ao_varrer_descarta_a_lista)
{
	uint8_t buffer[sizeof(csv)];

	write_file("a.csv", csv, sizeof(csv) - 1U);
	zassert_equal(1, storage_scan(tracks, ARRAY_SIZE(tracks), NULL));

	host_libc_fault_arm(HOST_LIBC_FAULT_READDIR);
	zassert_equal(-EIO, storage_scan(tracks, ARRAY_SIZE(tracks), NULL));
	zassert_equal(-ENOENT, storage_load(0, buffer, sizeof(buffer)));

	host_libc_fault_arm(HOST_LIBC_FAULT_FSTATAT);
	zassert_equal(-EIO, storage_scan(tracks, ARRAY_SIZE(tracks), NULL));
	zassert_equal(-ENOENT, storage_load(0, buffer, sizeof(buffer)));
}

ZTEST(storage_host, test_falhas_ao_ler_o_arquivo)
{
	uint8_t buffer[sizeof(csv)];

	write_file("a.csv", csv, sizeof(csv) - 1U);
	zassert_equal(1, storage_scan(tracks, ARRAY_SIZE(tracks), NULL));

	host_libc_fault_arm(HOST_LIBC_FAULT_FSTAT);
	zassert_equal(-EIO, storage_load(0, buffer, sizeof(buffer)));

	/* Interrupção por sinal não é falha: a leitura recomeça. */
	host_libc_fault_arm(HOST_LIBC_FAULT_READ_EINTR);
	zassert_equal(sizeof(csv) - 1U, storage_load(0, buffer, sizeof(buffer)));
	zassert_mem_equal(csv, buffer, sizeof(csv) - 1U);

	host_libc_fault_arm(HOST_LIBC_FAULT_READ_EIO);
	zassert_equal(-EIO, storage_load(0, buffer, sizeof(buffer)));

	/* O arquivo encurtou depois do fstat: carga incompleta é falha. */
	host_libc_fault_arm(HOST_LIBC_FAULT_READ_EOF);
	zassert_equal(-EIO, storage_load(0, buffer, sizeof(buffer)));

	/* Trocado por uma pasta depois da varredura. */
	zassert_ok(unlinkat(directory_fd, "a.csv", 0));
	zassert_ok(mkdirat(directory_fd, "a.csv", 0700));
	zassert_equal(-EIO, storage_load(0, buffer, sizeof(buffer)));
}

ZTEST(storage_host, test_pasta_muda_entre_varredura_e_carga)
{
	uint8_t buffer[sizeof(csv)];
	char path[sizeof(directory) + 16U];

	zassert_ok(mkdirat(directory_fd, "sub", 0700));
	write_file("sub/a.csv", csv, sizeof(csv) - 1U);
	snprintf(path, sizeof(path), "%s/sub", directory);
	zassert_ok(setenv("VITROLINHA_MUSIC_DIR", path, 1));
	zassert_equal(1, storage_scan(tracks, ARRAY_SIZE(tracks), NULL));

	/* A pasta sumiu. */
	zassert_ok(renameat(directory_fd, "sub", directory_fd, "movida"));
	zassert_equal(-ENODEV, storage_load(0, buffer, sizeof(buffer)));

	/* Um arquivo ocupa o lugar dela. */
	write_file("sub", csv, sizeof(csv) - 1U);
	zassert_equal(-ENODEV, storage_load(0, buffer, sizeof(buffer)));

	/* Um laço de links ocupa o lugar dela. */
	zassert_ok(unlinkat(directory_fd, "sub", 0));
	zassert_ok(symlinkat("sub", directory_fd, "sub"));
	zassert_equal(-EIO, storage_load(0, buffer, sizeof(buffer)));

	zassert_ok(unlinkat(directory_fd, "movida/a.csv", 0));
}

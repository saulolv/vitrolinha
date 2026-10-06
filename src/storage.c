/**
 * @file storage.c
 * @brief O @ref storage.h servido pelo cartão: a biblioteca é a raiz do volume.
 *
 * Duas decisões moldam este arquivo.
 *
 * **O nome da faixa vem do cabeçalho RTTTL**, não do sistema de arquivos. Sem
 * `CONFIG_FS_FATFS_LFN` a leitura de diretório devolve `FURELIS.TXT`, ruim
 * numa tela cujo único texto é o nome. Ler o cabeçalho custa uma abertura por
 * arquivo, feita uma vez por varredura; ligar nomes longos custaria RAM
 * estática para sempre.
 *
 * **A varredura pega o volume emprestado** (@ref card_acquire). O FatFs roda
 * sem reentrância e o monitor do cartão desmonta noutra thread; sem o
 * empréstimo, um cartão puxado no meio da varredura seria desmontado por
 * baixo de uma leitura em curso. O mesmo empréstimo serializa a varredura com
 * a carga, e é por isso que o estado deste arquivo não tem trava própria.
 *
 * @see docs/adr/0009-a-varredura-empresta-o-volume.md
 */

#include <vitrolinha/card.h>
#include <vitrolinha/storage.h>
#include <vitrolinha/track.h>

#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(storage, LOG_LEVEL_INF);

/**
 * @brief Quanto de cada arquivo é lido para achar o nome.
 *
 * O cabeçalho RTTTL começa pelo nome, terminado em `:`. O nome útil tem 16
 * caracteres na tela e o campo, 23 bytes; 64 B cobrem isso com folga para BOM,
 * espaços e um nome longo que vai ser truncado. Cabe num setor, então custa a
 * mesma leitura que um byte custaria.
 */
#define STORAGE_HEADER_PEEK 64U

/**
 * @brief Tamanho de um nome 8.3 com terminador, como o `fs_readdir` o entrega.
 *
 * Tirado da própria estrutura do Zephyr, e não escrito à mão: se alguém ligar
 * nomes longos, a tabela cresce junto em vez de truncar em silêncio.
 */
#define STORAGE_NAME_SIZE sizeof(((struct fs_dirent *)0)->name)

/** @brief `"/SD:/"` mais o nome 8.3 e o terminador. */
#define STORAGE_PATH_SIZE (sizeof(CARD_MOUNT_POINT) + STORAGE_NAME_SIZE)

/**
 * @brief Nome no sistema de arquivos de cada faixa da biblioteca.
 *
 * É a ponte entre o índice que o resto do sistema conhece e o arquivo que só
 * este módulo conhece. 32 nomes 8.3 são 416 B, fixos (RNF06). Só é tocada com
 * o volume emprestado.
 */
static char lib_files[LIBRARY_MAX][STORAGE_NAME_SIZE];

/** @brief Faixas da última varredura bem-sucedida. Zero depois de uma falha. */
static size_t lib_count;

/**
 * @brief Início de cada arquivo, onde se procura o nome.
 *
 * Estático, e não na pilha, para que o custo de pilha de quem varre não
 * dependa deste número. Só é tocado com o volume emprestado.
 */
static uint8_t peek[STORAGE_HEADER_PEEK];

/**
 * @brief Diz se a entrada de diretório é uma faixa.
 *
 * Só arquivos de extensão RTTTL. Sem nomes longos o FatFs entrega o nome 8.3
 * tal como está gravado no diretório, sempre em maiúsculas — daí a
 * comparação exata.
 *
 * O `_` inicial descarta as sobras que o macOS deixa em todo cartão: o
 * `._FURELIS.TXT` de metadados vira `_FURELI~1.TXT` em 8.3, com extensão de
 * faixa e conteúdo binário.
 *
 * O formato CSV de bancada não entra aqui: ele não tem cabeçalho de onde
 * tirar o nome, e a regra dele é da issue #22.
 *
 * @param entry Entrada devolvida por `fs_readdir`.
 * @return Verdadeiro se a entrada deve entrar na biblioteca.
 */
static bool is_track_file(const struct fs_dirent *entry)
{
	static const char *const extensions[] = {"TXT", "RTX", "RTT"};
	const char *dot;

	if ((entry->type != FS_DIR_ENTRY_FILE) || (entry->name[0] == '_')) {
		return false;
	}

	dot = strrchr(entry->name, '.');
	if (dot == NULL) {
		return false;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(extensions); i++) {
		if (strcmp(dot + 1, extensions[i]) == 0) {
			return true;
		}
	}

	return false;
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

/**
 * @brief Acha o nome no começo de um cabeçalho RTTTL.
 *
 * O nome é tudo antes do primeiro `:`. Em volta dele vale o que um editor de
 * texto costuma deixar: BOM de UTF-8 e linhas em branco antes, espaços dos
 * dois lados. Uma quebra de linha **antes** do `:` diz que aquilo não é
 * cabeçalho — o nome não ocupa duas linhas.
 *
 * @param buf      Início do arquivo.
 * @param len      Quantos bytes de @p buf foram lidos.
 * @param name     Recebe o começo do nome, dentro de @p buf.
 * @param name_len Recebe o comprimento do nome, possivelmente zero.
 * @return Verdadeiro se há cabeçalho; falso deixa as saídas intocadas.
 */
static bool header_name(const char *buf, size_t len, const char **name, size_t *name_len)
{
	static const char bom[] = "\xEF\xBB\xBF";
	size_t start = 0U;
	size_t end;
	size_t found;

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

	found = end - start;

	while ((found > 0U) && is_blank(buf[start + found - 1U])) {
		found--;
	}

	*name = &buf[start];
	*name_len = found;

	return true;
}

/**
 * @brief Lê o cabeçalho de uma faixa e preenche os metadados dela.
 *
 * Problema de **conteúdo** não é erro: a faixa entra na biblioteca marcada
 * como inválida, com o nome 8.3 quando não há nome legível, para aparecer na
 * lista com `!` em vez de sumir sem explicação. Já falha de **leitura** é
 * erro, porque diz que o cartão parou de responder — e aí a biblioteca
 * inteira está em dúvida, não esta faixa.
 *
 * @param entry Entrada do diretório.
 * @param index Posição na biblioteca.
 * @param meta  Destino.
 * @retval 0    Metadados preenchidos.
 * @retval -EIO O arquivo não pôde ser aberto ou lido.
 */
static int describe_track(const struct fs_dirent *entry, uint8_t index,
			  struct track_meta *meta)
{
	char path[STORAGE_PATH_SIZE];
	struct fs_file_t file;
	const char *name = entry->name;
	size_t name_len = strlen(entry->name);
	const char *header;
	size_t header_len;
	ssize_t got;
	bool valid;

	(void)snprintk(path, sizeof(path), "%s/%s", CARD_MOUNT_POINT, entry->name);

	fs_file_t_init(&file);

	if (fs_open(&file, path, FS_O_READ) != 0) {
		return -EIO;
	}

	got = fs_read(&file, peek, sizeof(peek));

	/* Só leitura: não há o que descarregar, e o erro que importaria já
	 * apareceu no fs_read.
	 */
	(void)fs_close(&file);

	if (got < 0) {
		return -EIO;
	}

	/* Arquivo vazio não tem cabeçalho, então já sai daqui inválido. O
	 * maior que o buffer é recusado agora, e não só na carga: o tamanho
	 * vem de graça na entrada de diretório, e a marca `!` na lista poupa
	 * a quem seleciona a faixa uma tela de erro.
	 */
	valid = header_name((const char *)peek, (size_t)got, &header, &header_len);

	if (valid && (header_len > 0U)) {
		name = header;
		name_len = header_len;
	}

	valid = valid && (entry->size <= STORAGE_FILE_MAX);

	track_meta_init(meta, index, name, name_len, valid);

	return 0;
}

/**
 * @brief Percorre a raiz do volume e monta a biblioteca.
 *
 * Passado o teto, os arquivos seguintes só são contados: a contagem custa um
 * `fs_readdir` e nenhuma abertura.
 *
 * @param out     Destino dos metadados.
 * @param max     Capacidade de @p out, já limitada a ::LIBRARY_MAX.
 * @param skipped Recebe quantas faixas não couberam.
 * @return Quantidade de faixas, ou `-EIO`.
 */
static int scan_root(struct track_meta *out, size_t max, size_t *skipped)
{
	struct fs_dir_t dir;
	struct fs_dirent entry;
	size_t count = 0U;
	int err = 0;

	fs_dir_t_init(&dir);

	if (fs_opendir(&dir, CARD_MOUNT_POINT) != 0) {
		return -EIO;
	}

	while (err == 0) {
		if (fs_readdir(&dir, &entry) != 0) {
			err = -EIO;
		} else if (entry.name[0] == '\0') {
			/* Fim do diretório. */
			break;
		} else if (!is_track_file(&entry)) {
			continue;
		} else if (count == max) {
			(*skipped)++;
		} else {
			err = describe_track(&entry, (uint8_t)count, &out[count]);

			if (err == 0) {
				(void)memcpy(lib_files[count], entry.name,
					     sizeof(lib_files[count]));
				count++;
			}
		}
	}

	(void)fs_closedir(&dir);

	return (err != 0) ? err : (int)count;
}

int storage_scan(struct track_meta *out, size_t max, size_t *skipped)
{
	size_t ignored = 0U;
	int result;

	if (skipped != NULL) {
		*skipped = 0U;
	}

	if ((out == NULL) || (max == 0U)) {
		return -EINVAL;
	}

	/* O teto da biblioteca vale mesmo que o chamador ofereça mais espaço:
	 * é ele que mantém o consumo de RAM previsível (RNF06).
	 */
	if (max > LIBRARY_MAX) {
		max = LIBRARY_MAX;
	}

	result = card_acquire();
	if (result != 0) {
		return result;
	}

	result = scan_root(out, max, &ignored);

	/* Uma varredura que falhou não deixa para trás índices de uma
	 * biblioteca que pode nem ser mais a do soquete.
	 */
	lib_count = (result > 0) ? (size_t)result : 0U;

	card_release();

	if (result < 0) {
		LOG_ERR("Varredura falhou: %d", result);
		return result;
	}

	if (ignored > 0U) {
		LOG_WRN("Biblioteca cheia: %zu arquivo(s) alem dos %zu ignorado(s)", ignored,
			max);
	}

	LOG_INF("Biblioteca: %d faixa(s)", result);

	if (skipped != NULL) {
		*skipped = ignored;
	}

	return result;
}

int storage_load(int track, uint8_t *buf, size_t len)
{
	int err;

	if ((buf == NULL) || (len == 0U)) {
		return -EINVAL;
	}

	err = card_acquire();
	if (err != 0) {
		return err;
	}

	if ((track < 0) || ((size_t)track >= lib_count)) {
		err = -ENOENT;
	} else {
		/* A carga integral é a issue #11. O que ela precisa daqui já
		 * existe: o nome do arquivo de cada faixa está em lib_files.
		 */
		err = -ENOTSUP;
	}

	card_release();

	return err;
}

bool storage_present(void)
{
	return card_ready();
}

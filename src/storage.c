/**
 * @file storage.c
 * @brief O @ref storage.h servido pelo cartão: a biblioteca é a raiz do volume.
 *
 * Três decisões moldam este arquivo.
 *
 * **As faixas são os arquivos de música da raiz**: RTTTL em `.TXT`, `.RTX`
 * ou `.RTT`, e CSV de bancada em `.CSV`. O nome exibido do RTTTL sai do
 * cabeçalho dele (@ref track_rtttl_name): sem `CONFIG_FS_FATFS_LFN` o
 * diretório entrega o nome em 8.3 e em maiúsculas, e ler o cabeçalho evita a
 * RAM estática dos nomes longos. O CSV não tem campo de nome e fica com o do
 * arquivo sem a extensão (`FURELISE.CSV` vira `FURELISE`). A varredura abre
 * cada arquivo para conferir o começo: o que não tem o cabeçalho do formato
 * entra marcado como inválido, em vez de só falhar quando for selecionado.
 *
 * **A faixa é carregada inteira, de uma vez**, antes de tocar. O tamanho é
 * conferido na entrada de diretório antes de ler um byte, para que o arquivo
 * grande seja recusado sem deixar meia melodia no buffer. Devolvida a carga,
 * a reprodução não precisa mais deste módulo nem do cartão.
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
 * @brief Primeira linha de toda faixa, sem o fim de linha.
 *
 * A mesma que o `parser_csv` exige, byte a byte: sem BOM, sem espaços, sem
 * maiúsculas. Conferir aqui com regra mais frouxa listaria como válida uma
 * faixa que a carga recusaria.
 */
#define STORAGE_CSV_HEADER "frequency_hz,duration_ms"

/** @brief Extensão do CSV, como o FatFs a entrega sem nomes longos. */
#define STORAGE_CSV_EXTENSION ".CSV"

/**
 * @brief Quanto de cada arquivo é lido para conferir o cabeçalho.
 *
 * O bastante para o nome do RTTTL, e para o cabeçalho do CSV com o CRLF que
 * um editor do Windows deixa depois dele. Cabe num setor, então custa a
 * mesma leitura que um byte custaria.
 */
#define STORAGE_HEADER_PEEK MAX(TRACK_RTTTL_PEEK, sizeof(STORAGE_CSV_HEADER) + 1U)

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
 * @brief Início de cada arquivo, onde se confere o cabeçalho.
 *
 * Estático, e não na pilha, para que o custo de pilha de quem varre não
 * dependa deste número. Só é tocado com o volume emprestado.
 */
static uint8_t peek[STORAGE_HEADER_PEEK];

/**
 * @brief Monta o caminho de um arquivo da raiz do volume.
 *
 * @param path Destino, de ::STORAGE_PATH_SIZE bytes — o que cabe qualquer
 *             nome que o `fs_readdir` entregue.
 * @param name Nome 8.3, como o `fs_readdir` o entregou.
 */
static void track_path(char path[STORAGE_PATH_SIZE], const char *name)
{
	(void)snprintk(path, STORAGE_PATH_SIZE, "%s/%s", CARD_MOUNT_POINT, name);
}

/**
 * @brief Diz se a entrada de diretório é uma faixa.
 *
 * Só as extensões de música: `.TXT`, `.RTX`, `.RTT` e `.CSV`. Sem nomes
 * longos o FatFs entrega o nome 8.3 tal como está gravado no diretório,
 * sempre em maiúsculas — daí a comparação exata.
 *
 * O `_` inicial descarta as sobras que o macOS deixa em todo cartão: o
 * `._FURELISE.CSV` de metadados vira `_FURELI~1.CSV` em 8.3, com extensão de
 * faixa e conteúdo binário.
 *
 * @param entry Entrada devolvida por `fs_readdir`.
 * @return Verdadeiro se a entrada deve entrar na biblioteca.
 */
static bool is_track_file(const struct fs_dirent *entry)
{
	static const char *const extensions[] = {".TXT", ".RTX", ".RTT", STORAGE_CSV_EXTENSION};
	const char *dot;

	if ((entry->type != FS_DIR_ENTRY_FILE) || (entry->name[0] == '_')) {
		return false;
	}

	dot = strrchr(entry->name, '.');
	if (dot == NULL) {
		return false;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(extensions); i++) {
		if (strcmp(dot, extensions[i]) == 0) {
			return true;
		}
	}

	return false;
}

/**
 * @brief Diz se o arquivo começa pelo cabeçalho do CSV.
 *
 * O cabeçalho seguido de LF ou de CRLF, as duas quebras que o parser aceita.
 * Só o cabeçalho, sem quebra, também não vale: o parser exige ao menos uma
 * linha musical depois dele.
 *
 * @param buf Início do arquivo.
 * @param len Quantos bytes de @p buf foram lidos.
 * @return Verdadeiro se a primeira linha é o cabeçalho.
 */
static bool has_csv_header(const uint8_t *buf, size_t len)
{
	const size_t header_len = sizeof(STORAGE_CSV_HEADER) - 1U;

	if ((len <= header_len) || (memcmp(buf, STORAGE_CSV_HEADER, header_len) != 0)) {
		return false;
	}

	if (buf[header_len] == '\n') {
		return true;
	}

	return (buf[header_len] == '\r') && (len > (header_len + 1U)) &&
	       (buf[header_len + 1U] == '\n');
}

/**
 * @brief Lê o começo de uma faixa e preenche os metadados dela.
 *
 * Problema de **conteúdo** não é erro: a faixa entra na biblioteca marcada
 * como inválida, para aparecer na lista em vez de sumir sem explicação. Já
 * falha de **leitura** é erro, porque diz que o cartão parou de responder — e
 * aí a biblioteca inteira está em dúvida, não esta faixa.
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
	const char *dot = strrchr(entry->name, '.');
	const char *name = entry->name;
	size_t name_len = (size_t)(dot - entry->name);
	ssize_t got;
	bool valid;

	track_path(path, entry->name);

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

	/* O vazio e o sem cabeçalho já saem daqui inválidos. O maior que o
	 * buffer é recusado agora, e não só na carga: o tamanho vem de graça
	 * na entrada de diretório, e a marca na lista poupa a quem seleciona a
	 * faixa uma tela de erro.
	 */
	if (strcmp(dot, STORAGE_CSV_EXTENSION) == 0) {
		valid = has_csv_header(peek, (size_t)got);
	} else {
		const char *header;
		size_t header_len;

		/* Sem nome no cabeçalho (`:d=4,...`), fica o do arquivo. */
		valid = track_rtttl_name((const char *)peek, (size_t)got, &header, &header_len);
		if (valid && (header_len > 0U)) {
			name = header;
			name_len = header_len;
		}
	}

	valid = valid && (entry->size <= STORAGE_FILE_MAX);
	track_meta_init(meta, index, name, name_len, valid);

	return 0;
}

/**
 * @brief Lê um arquivo da biblioteca, inteiro, para @p buf.
 *
 * O tamanho vem da entrada de diretório, **antes** da leitura. É o que permite
 * recusar o arquivo grande sem escrever nada em @p buf: ler até encher e só
 * então descobrir que sobrou deixaria no buffer meia melodia, justamente o
 * que o contrato promete não fazer.
 *
 * A leitura é um laço porque o `fs_read` pode devolver menos que o pedido. O
 * FatFs só faz isso no fim do arquivo, mas o contrato do Zephyr não promete
 * mais que "até tantos bytes" — e um arquivo que acaba antes do que a entrada
 * de diretório anunciou é volume inconsistente, não faixa curta.
 *
 * @param name Nome 8.3 do arquivo.
 * @param buf  Destino.
 * @param len  Capacidade de @p buf, já limitada a ::STORAGE_FILE_MAX.
 * @return Bytes lidos, ou erro negativo:
 * @retval -ENOENT O arquivo não existe mais no volume.
 * @retval -EFBIG  Não cabe em @p len. @p buf intocado.
 * @retval -EIO    Falha de leitura, ou arquivo menor que o anunciado.
 */
static int load_file(const char *name, uint8_t *buf, size_t len)
{
	char path[STORAGE_PATH_SIZE];
	struct fs_dirent entry;
	struct fs_file_t file;
	size_t total = 0U;
	ssize_t got;
	int err;

	track_path(path, name);

	/* Sumir entre a varredura e a carga é o cartão trocado sem que a
	 * biblioteca tenha sido varrida de novo: a faixa não está mais nela.
	 */
	err = fs_stat(path, &entry);
	if (err != 0) {
		return (err == -ENOENT) ? -ENOENT : -EIO;
	}

	if (entry.size > len) {
		return -EFBIG;
	}

	fs_file_t_init(&file);

	if (fs_open(&file, path, FS_O_READ) != 0) {
		return -EIO;
	}

	while (total < entry.size) {
		got = fs_read(&file, &buf[total], entry.size - total);
		if (got <= 0) {
			break;
		}

		total += (size_t)got;
	}

	/* Só leitura: não há o que descarregar, e o erro que importaria já
	 * apareceu no fs_read.
	 */
	(void)fs_close(&file);

	return (total == entry.size) ? (int)total : -EIO;
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
	int result;

	if ((buf == NULL) || (len == 0U)) {
		return -EINVAL;
	}

	/* O teto do buffer de faixa vale mesmo que o chamador ofereça mais
	 * espaço: é o mesmo critério com que a varredura marcou a faixa, e uma
	 * faixa listada com `!` não pode carregar só porque o buffer de quem
	 * pediu era maior.
	 */
	if (len > STORAGE_FILE_MAX) {
		len = STORAGE_FILE_MAX;
	}

	result = card_acquire();
	if (result != 0) {
		return result;
	}

	if ((track < 0) || ((size_t)track >= lib_count)) {
		result = -ENOENT;
	} else {
		result = load_file(lib_files[track], buf, len);
	}

	/* Devolvido aqui, e não depois de tocar: a partir deste ponto a faixa
	 * está inteira em RAM, e o cartão pode sair do soquete sem que a
	 * reprodução perceba (RNF07).
	 */
	card_release();

	if (result == -EFBIG) {
		LOG_WRN("Faixa %d recusada: maior que %zu B", track, len);
	} else if (result < 0) {
		LOG_ERR("Carga da faixa %d falhou: %d", track, result);
	}

	return result;
}

bool storage_present(void)
{
	return card_ready();
}

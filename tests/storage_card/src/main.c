/**
 * @file main.c
 * @brief Testes do @ref storage.h servido pelo cartão.
 *
 * O critério da issue #10 é "a lista real do cartão aparece com os nomes
 * legíveis, e o 33.º arquivo é ignorado com aviso". Aqui a lista é real no
 * que importa: arquivos de verdade num volume FAT de verdade, lidos pelo
 * FatFs de verdade através do módulo `card`. O que troca é o disco embaixo —
 * o mesmo de tests/card/, que o teste põe, tira e faz falhar.
 *
 * Os nomes de arquivo são 8.3 porque o firmware roda sem nomes longos: é
 * exatamente por isso que o nome da faixa vem do cabeçalho.
 *
 * O critério da issue #11 é "o arquivo carrega, o oversize é rejeitado com
 * mensagem, e a reprodução não emite nenhuma chamada de sistema de arquivos".
 * O último vira asserção pelo contador de leituras do disco: depois da carga,
 * o cartão sai do soquete, a faixa é percorrida do byte zero, e o disco não
 * vê leitura nenhuma.
 */

#include "fake_disk.h"

#include <vitrolinha/card.h>
#include <vitrolinha/storage.h>
#include <vitrolinha/track.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

/** @brief Quantas entradas a limpeza da raiz consegue apagar por caso. */
#define WIPE_MAX 48U

/** @brief Teto de pontos de remoção tentados na varredura interrompida. */
#define EJECT_SWEEP_MAX 1000U

/** @brief Um cabeçalho RTTTL mínimo e válido, depois do nome. */
#define BODY ":d=4,o=5,b=125:c,d,e"

/**
 * @brief Uma melodia de verdade, do tamanho típico do que vai no cartão.
 *
 * Com o fim de linha que um editor de texto deixa: a carga entrega o arquivo
 * como está, e quem decide o que é RTTTL é o interpretador.
 */
#define FUR_ELISE                                                                          \
	"Fur Elise:d=8,o=5,b=125:"                                                         \
	"e6,d#6,e6,d#6,e6,b,d6,c6,a,p,c,e,a,b,p,e,g#,b,c6,"                                \
	"p,e,e6,d#6,e6,d#6,e6,b,d6,c6,a,p,c,e,a,b,p,e,c6,b,a\r\n"

/** @brief Byte que marca o buffer, para ver se a carga recusada o tocou. */
#define UNTOUCHED 0xAAU

/** @brief Conteúdo grande, para os casos de tamanho. Estático: não cabe na pilha. */
static char big[STORAGE_FILE_MAX + 1U];

/**
 * @brief Destino da carga: estático e do tamanho do buffer de faixa, como na
 *        aplicação.
 */
static uint8_t track_buf[STORAGE_FILE_MAX];

/**
 * @brief Monta o caminho de um arquivo na raiz do cartão.
 */
static void card_path(char *path, size_t size, const char *name)
{
	(void)snprintf(path, size, "%s/%s", CARD_MOUNT_POINT, name);
}

/**
 * @brief Escreve um arquivo na raiz do cartão.
 *
 * @param name    Nome 8.3.
 * @param content Conteúdo; não precisa de terminador.
 * @param len     Quantos bytes escrever.
 */
static void put_file(const char *name, const void *content, size_t len)
{
	char path[32];
	struct fs_file_t file;

	card_path(path, sizeof(path), name);
	fs_file_t_init(&file);

	zassert_ok(fs_open(&file, path, FS_O_CREATE | FS_O_WRITE), "nao abriu %s", path);
	zassert_equal((ssize_t)len, fs_write(&file, content, len), "nao escreveu %s", path);
	zassert_ok(fs_close(&file));
}

/** @brief Escreve um arquivo de texto. */
static void put_text(const char *name, const char *text)
{
	put_file(name, text, strlen(text));
}

/**
 * @brief Apaga tudo o que houver na raiz do cartão.
 *
 * Os nomes são juntados antes de apagar: remover entradas do diretório
 * enquanto ele é percorrido é pedir para pular uma.
 */
static void wipe_root(void)
{
	static char names[WIPE_MAX][sizeof(((struct fs_dirent *)0)->name)];
	struct fs_dir_t dir;
	struct fs_dirent entry;
	char path[32];
	size_t count = 0U;

	fs_dir_t_init(&dir);
	zassert_ok(fs_opendir(&dir, CARD_MOUNT_POINT));

	while ((fs_readdir(&dir, &entry) == 0) && (entry.name[0] != '\0')) {
		zassert_true(count < WIPE_MAX, "raiz com entradas demais para limpar");
		(void)strcpy(names[count], entry.name);
		count++;
	}

	zassert_ok(fs_closedir(&dir));

	for (size_t i = 0U; i < count; i++) {
		card_path(path, sizeof(path), names[i]);
		zassert_ok(fs_unlink(path), "nao apagou %s", path);
	}
}

/**
 * @brief Tira o cartão e o devolve, saudável e montado.
 */
static void reinsert(void)
{
	fake_disk_reset();
	zassert_equal(CARD_ABSENT, card_refresh(), "o soquete nao esvaziou");

	fake_disk_insert(true);
	zassert_equal(CARD_READY, card_refresh(), "o cartao nao montou");
}

/** @brief Escreve @p count faixas de nomes `T00.TXT`... e `Faixa 00`... */
static void put_numbered_tracks(size_t count)
{
	char name[16];
	char text[48];

	for (size_t i = 0U; i < count; i++) {
		(void)snprintf(name, sizeof(name), "T%02u.TXT", (unsigned int)i);
		(void)snprintf(text, sizeof(text), "Faixa %02u" BODY, (unsigned int)i);
		put_text(name, text);
	}
}

static void *storage_card_setup(void)
{
	zassert_ok(fake_disk_setup(), "o disco de mentira nao subiu");
	zassert_ok(card_init(), "o monitor do cartao nao subiu");

	return NULL;
}

/** @brief Todo caso começa com o cartão montado, saudável e com a raiz vazia. */
static void storage_card_before(void *fixture)
{
	ARG_UNUSED(fixture);

	reinsert();
	wipe_root();
}

ZTEST_SUITE(storage_card, NULL, storage_card_setup, storage_card_before, NULL, NULL);

/* -------------------------------------------------------------------------
 * O nome vem do cabeçalho
 * ------------------------------------------------------------------------- */

ZTEST(storage_card, test_cartao_vazio_da_biblioteca_vazia)
{
	struct track_meta library[LIBRARY_MAX];
	size_t skipped = 99U;

	zassert_equal(0, storage_scan(library, ARRAY_SIZE(library), &skipped));
	zassert_equal(0U, skipped);
}

ZTEST(storage_card, test_nome_vem_do_cabecalho_e_nao_do_arquivo)
{
	struct track_meta library[LIBRARY_MAX];

	/* O caso que motiva a issue: sem nomes longos, o diretório diz
	 * FURELIS.TXT, e a tela tem de dizer Fur Elise.
	 */
	put_text("FURELIS.TXT", "Fur Elise:d=8,o=5,b=125:e6,d#6,e6");

	zassert_equal(1, storage_scan(library, ARRAY_SIZE(library), NULL));
	zassert_str_equal("Fur Elise", library[0].name);
	zassert_equal(0U, library[0].index);
	zassert_true(library[0].valid);
}

ZTEST(storage_card, test_faixas_saem_na_ordem_do_diretorio)
{
	struct track_meta library[LIBRARY_MAX];

	/* Ordem de cópia, não alfabética: é ela que dá sentido a "o 33.º
	 * arquivo", e é a que quem copiou os arquivos espera ver.
	 */
	put_text("C.TXT", "Charlie" BODY);
	put_text("A.TXT", "Alfa" BODY);
	put_text("B.TXT", "Bravo" BODY);

	zassert_equal(3, storage_scan(library, ARRAY_SIZE(library), NULL));
	zassert_str_equal("Charlie", library[0].name);
	zassert_str_equal("Alfa", library[1].name);
	zassert_str_equal("Bravo", library[2].name);

	for (int i = 0; i < 3; i++) {
		zassert_equal((uint8_t)i, library[i].index, "o indice tem de casar com a posicao");
	}
}

ZTEST(storage_card, test_extensoes_de_rtttl)
{
	struct track_meta library[LIBRARY_MAX];

	put_text("UM.TXT", "Um" BODY);
	put_text("DOIS.RTX", "Dois" BODY);
	put_text("TRES.RTT", "Tres" BODY);

	zassert_equal(3, storage_scan(library, ARRAY_SIZE(library), NULL));
	zassert_str_equal("Dois", library[1].name);
	zassert_str_equal("Tres", library[2].name);
}

ZTEST(storage_card, test_o_que_nao_e_faixa_fica_de_fora)
{
	struct track_meta library[LIBRARY_MAX];
	size_t skipped = 99U;

	put_text("DADOS.CSV", "440,500\n");
	put_text("IMAGEM.BIN", "Nao e faixa" BODY);
	put_text("LEIAME", "Sem extensao" BODY);
	/* A sobra de metadados que o macOS deixa em todo cartão. */
	put_file("_FURE~1.TXT", "\x00\x05\x16\x07", 4U);
	zassert_ok(fs_mkdir(CARD_MOUNT_POINT "/PASTA.TXT"), "nao criou o diretorio");
	put_text("FAIXA.TXT", "A unica" BODY);

	/* Capacidade de uma faixa só: se algum dos de cima contasse como
	 * faixa, apareceria no excedente.
	 */
	zassert_equal(1, storage_scan(library, 1U, &skipped));
	zassert_str_equal("A unica", library[0].name);
	zassert_equal(0U, skipped, "o que nao e faixa entrou na conta do excedente");
}

/* -------------------------------------------------------------------------
 * O teto da biblioteca
 * ------------------------------------------------------------------------- */

ZTEST(storage_card, test_33a_faixa_e_ignorada_com_aviso)
{
	struct track_meta library[LIBRARY_MAX];
	size_t skipped = 0U;

	/* O critério da issue, literalmente. */
	put_numbered_tracks(LIBRARY_MAX + 1U);

	zassert_equal((int)LIBRARY_MAX, storage_scan(library, ARRAY_SIZE(library), &skipped));
	zassert_equal(1U, skipped, "a 33a faixa tinha de ser contada como ignorada");
	zassert_str_equal("Faixa 31", library[LIBRARY_MAX - 1U].name);
}

ZTEST(storage_card, test_excedente_pode_ser_dispensado)
{
	struct track_meta library[LIBRARY_MAX];

	put_numbered_tracks(LIBRARY_MAX + 1U);

	zassert_equal((int)LIBRARY_MAX, storage_scan(library, ARRAY_SIZE(library), NULL));
}

ZTEST(storage_card, test_capacidade_do_chamador_conta_no_excedente)
{
	struct track_meta library[1];
	size_t skipped = 0U;

	put_numbered_tracks(3U);

	zassert_equal(1, storage_scan(library, ARRAY_SIZE(library), &skipped));
	zassert_str_equal("Faixa 00", library[0].name);
	zassert_equal(2U, skipped);
}

ZTEST(storage_card, test_capacidade_acima_do_teto_vale_o_teto)
{
	struct track_meta library[LIBRARY_MAX];
	size_t skipped = 0U;

	/* Capacidade declarada acima do teto não pode fazer a varredura
	 * escrever além de LIBRARY_MAX (RNF06).
	 */
	put_numbered_tracks(LIBRARY_MAX + 1U);

	zassert_equal((int)LIBRARY_MAX, storage_scan(library, LIBRARY_MAX * 4U, &skipped));
	zassert_equal(1U, skipped);
}

/* -------------------------------------------------------------------------
 * Cabeçalhos tortos
 * ------------------------------------------------------------------------- */

ZTEST(storage_card, test_sem_cabecalho_fica_invalida_com_nome_do_arquivo)
{
	struct track_meta library[LIBRARY_MAX];

	/* Listada com `!`, e com um nome que o usuário reconhece, em vez de
	 * sumir da lista sem explicação.
	 */
	put_text("SEMCAB.TXT", "isto nao e rtttl nenhum");

	zassert_equal(1, storage_scan(library, ARRAY_SIZE(library), NULL));
	zassert_str_equal("SEMCAB.TXT", library[0].name);
	zassert_false(library[0].valid);
}

ZTEST(storage_card, test_quebra_de_linha_antes_dos_dois_pontos)
{
	struct track_meta library[LIBRARY_MAX];

	/* O nome não ocupa duas linhas: o `:` da segunda linha não faz da
	 * primeira um nome.
	 */
	put_text("DUASLIN.TXT", "primeira linha\nNome" BODY);

	zassert_equal(1, storage_scan(library, ARRAY_SIZE(library), NULL));
	zassert_str_equal("DUASLIN.TXT", library[0].name);
	zassert_false(library[0].valid);
}

ZTEST(storage_card, test_dois_pontos_alem_da_janela)
{
	struct track_meta library[LIBRARY_MAX];

	/* Setenta caracteres antes do `:` passam da janela de leitura: aquilo
	 * não é o nome curto de um cabeçalho RTTTL.
	 */
	put_text("LONGE.TXT", "0123456789012345678901234567890123456789"
			      "012345678901234567890123456789" BODY);

	zassert_equal(1, storage_scan(library, ARRAY_SIZE(library), NULL));
	zassert_false(library[0].valid);
}

ZTEST(storage_card, test_nome_vazio_usa_o_arquivo_mas_vale)
{
	struct track_meta library[LIBRARY_MAX];

	/* Cabeçalho legível sem nome: a melodia toca, só não tem como se
	 * chamar de outro jeito.
	 */
	put_text("VAZIO.TXT", BODY);

	zassert_equal(1, storage_scan(library, ARRAY_SIZE(library), NULL));
	zassert_str_equal("VAZIO.TXT", library[0].name);
	zassert_true(library[0].valid);
}

ZTEST(storage_card, test_bom_linhas_e_espacos_sao_aparados)
{
	struct track_meta library[LIBRARY_MAX];

	/* O que um editor de texto do Windows costuma deixar no arquivo. */
	put_text("EDITOR.TXT", "\xEF\xBB\xBF\r\n\t Ode to Joy \t" BODY);

	zassert_equal(1, storage_scan(library, ARRAY_SIZE(library), NULL));
	zassert_str_equal("Ode to Joy", library[0].name);
	zassert_true(library[0].valid);
}

ZTEST(storage_card, test_so_espacos_nao_e_cabecalho)
{
	struct track_meta library[LIBRARY_MAX];

	put_text("BRANCO.TXT", "  \r\n\t ");

	zassert_equal(1, storage_scan(library, ARRAY_SIZE(library), NULL));
	zassert_false(library[0].valid);
}

ZTEST(storage_card, test_arquivo_menor_que_o_bom)
{
	struct track_meta library[LIBRARY_MAX];

	/* Dois bytes: menos do que o BOM ocupa, e ainda assim um cabeçalho. */
	put_text("CURTO.TXT", "a:");

	zassert_equal(1, storage_scan(library, ARRAY_SIZE(library), NULL));
	zassert_str_equal("a", library[0].name);
	zassert_true(library[0].valid);
}

ZTEST(storage_card, test_nome_longo_e_truncado)
{
	struct track_meta library[LIBRARY_MAX];

	put_text("LONGO.TXT", "Um nome de melodia mais longo que o campo" BODY);

	zassert_equal(1, storage_scan(library, ARRAY_SIZE(library), NULL));
	zassert_equal(TRACK_NAME_MAX - 1U, strlen(library[0].name));
	zassert_true(library[0].valid);
}

/* -------------------------------------------------------------------------
 * Tamanho do arquivo
 * ------------------------------------------------------------------------- */

ZTEST(storage_card, test_arquivo_vazio_fica_invalido)
{
	struct track_meta library[LIBRARY_MAX];

	put_file("NADA.TXT", "", 0U);

	zassert_equal(1, storage_scan(library, ARRAY_SIZE(library), NULL));
	zassert_str_equal("NADA.TXT", library[0].name);
	zassert_false(library[0].valid);
}

ZTEST(storage_card, test_arquivo_maior_que_o_buffer_fica_invalido)
{
	struct track_meta library[LIBRARY_MAX];

	/* Um byte além do buffer de faixa: a carga o rejeitaria, e marcar
	 * agora poupa a tela de erro a quem o selecionasse. O nome continua
	 * sendo o do cabeçalho.
	 */
	(void)memset(big, 'c', sizeof(big));
	(void)memcpy(big, "Grande" BODY, strlen("Grande" BODY));
	put_file("GRANDE.TXT", big, STORAGE_FILE_MAX + 1U);

	zassert_equal(1, storage_scan(library, ARRAY_SIZE(library), NULL));
	zassert_str_equal("Grande", library[0].name);
	zassert_false(library[0].valid);
}

ZTEST(storage_card, test_arquivo_do_tamanho_do_buffer_vale)
{
	struct track_meta library[LIBRARY_MAX];

	(void)memset(big, 'c', sizeof(big));
	(void)memcpy(big, "Cheio" BODY, strlen("Cheio" BODY));
	put_file("CHEIO.TXT", big, STORAGE_FILE_MAX);

	zassert_equal(1, storage_scan(library, ARRAY_SIZE(library), NULL));
	zassert_true(library[0].valid, "o teto do buffer e inclusivo");
}

/* -------------------------------------------------------------------------
 * Erros do contrato
 * ------------------------------------------------------------------------- */

ZTEST(storage_card, test_varredura_recusa_argumentos_invalidos)
{
	struct track_meta library[LIBRARY_MAX];
	size_t skipped = 99U;

	zassert_equal(-EINVAL, storage_scan(NULL, ARRAY_SIZE(library), &skipped));
	zassert_equal(0U, skipped);
	zassert_equal(-EINVAL, storage_scan(library, 0U, NULL));
}

ZTEST(storage_card, test_varredura_sem_cartao)
{
	struct track_meta library[LIBRARY_MAX];
	size_t skipped = 99U;

	fake_disk_insert(false);
	zassert_equal(CARD_ABSENT, card_refresh());

	zassert_equal(-ENODEV, storage_scan(library, ARRAY_SIZE(library), &skipped));
	zassert_equal(0U, skipped);
	zassert_false(storage_present());
}

ZTEST(storage_card, test_varredura_com_volume_ilegivel)
{
	struct track_meta library[LIBRARY_MAX];

	fake_disk_insert(false);
	zassert_equal(CARD_ABSENT, card_refresh());
	fake_disk_fail_read(true);
	fake_disk_insert(true);
	zassert_equal(CARD_UNREADABLE, card_refresh());

	/* Presente: a mensagem é "cartao ilegivel", não "sem cartao". */
	zassert_equal(-EIO, storage_scan(library, ARRAY_SIZE(library), NULL));
	zassert_false(storage_present());
}

ZTEST(storage_card, test_cartao_que_para_de_ler_depois_de_montar)
{
	struct track_meta library[LIBRARY_MAX];

	put_text("FAIXA.TXT", "Faixa" BODY);
	zassert_equal(1, storage_scan(library, ARRAY_SIZE(library), NULL));

	/* O volume segue montado e o diretório, em cache; quem descobre que o
	 * cartão parou de responder é a leitura do cabeçalho.
	 */
	fake_disk_fail_read(true);

	zassert_equal(-EIO, storage_scan(library, ARRAY_SIZE(library), NULL));
}

ZTEST(storage_card, test_remocao_em_qualquer_ponto_da_varredura)
{
	struct track_meta library[LIBRARY_MAX];
	uint8_t buf[16];
	unsigned int accesses;
	int result = -EIO;

	put_numbered_tracks(3U);

	/* O cartão sai depois de 0, 1, 2... acessos ao disco, até a varredura
	 * passar inteira. Cada ponto de interrupção possível é exercitado sem
	 * o teste precisar saber em qual chamada do FatFs ele cai — e em todos
	 * a resposta tem de ser um erro limpo, nunca uma biblioteca pela
	 * metade.
	 */
	for (accesses = 0U; accesses < EJECT_SWEEP_MAX; accesses++) {
		fake_disk_eject_after(accesses);
		result = storage_scan(library, ARRAY_SIZE(library), NULL);

		if (result >= 0) {
			break;
		}

		zassert_equal(-EIO, result, "remocao apos %u acessos deu %d", accesses, result);

		/* E a falha não deixa índices de uma biblioteca que talvez nem
		 * seja mais a do soquete.
		 */
		reinsert();
		zassert_equal(-ENOENT, storage_load(0, buf, sizeof(buf)),
			      "a varredura que falhou deixou biblioteca para tras");
	}

	zassert_equal(3, result, "a varredura nunca chegou ao fim");
	zassert_true(accesses > 0U, "nenhum ponto de remocao foi exercitado");
	zassert_str_equal("Faixa 02", library[2].name);
}

/* -------------------------------------------------------------------------
 * Presença
 * ------------------------------------------------------------------------- */

ZTEST(storage_card, test_presenca_acompanha_o_cartao)
{
	zassert_true(storage_present());

	fake_disk_insert(false);
	zassert_equal(CARD_ABSENT, card_refresh());

	zassert_false(storage_present());
}

/* -------------------------------------------------------------------------
 * Carga: o arquivo inteiro, de uma vez
 * ------------------------------------------------------------------------- */

/**
 * @brief Preenche @p buf com um padrão que muda a cada setor.
 *
 * Um aglomerado lido fora de ordem, ou duas vezes, produz bytes diferentes
 * do esperado — o que um preenchimento constante esconderia.
 */
static void fill_pattern(char *buf, size_t len, uint8_t seed)
{
	for (size_t i = 0U; i < len; i++) {
		buf[i] = (char)(uint8_t)(i ^ (i >> 8) ^ seed);
	}
}

/** @brief Marca o buffer de carga inteiro com ::UNTOUCHED. */
static void mark_track_buf(void)
{
	(void)memset(track_buf, UNTOUCHED, sizeof(track_buf));
}

/** @brief Confere que nenhum byte do buffer de carga foi escrito. */
static void assert_track_buf_untouched(void)
{
	for (size_t i = 0U; i < sizeof(track_buf); i++) {
		zassert_equal(UNTOUCHED, track_buf[i], "byte %zu escrito numa carga recusada", i);
	}
}

/** @brief Varre e confere que a biblioteca tem @p count faixas. */
static void scan_expecting(int count)
{
	struct track_meta library[LIBRARY_MAX];

	zassert_equal(count, storage_scan(library, ARRAY_SIZE(library), NULL));
}

ZTEST(storage_card, test_carga_traz_o_arquivo_inteiro)
{
	put_text("FURELIS.TXT", FUR_ELISE);
	scan_expecting(1);

	zassert_equal((int)strlen(FUR_ELISE), storage_load(0, track_buf, sizeof(track_buf)));
	zassert_mem_equal(FUR_ELISE, track_buf, strlen(FUR_ELISE));
}

ZTEST(storage_card, test_carga_segue_o_indice_da_varredura)
{
	put_text("C.TXT", "Charlie" BODY);
	put_text("A.TXT", "Alfa" BODY);
	put_text("B.TXT", "Bravo" BODY);
	scan_expecting(3);

	/* O índice 1 é o segundo arquivo do diretório, não o segundo em ordem
	 * alfabética: é o que a lista mostrou na posição 1.
	 */
	zassert_equal((int)strlen("Alfa" BODY), storage_load(1, track_buf, sizeof(track_buf)));
	zassert_mem_equal("Alfa" BODY, track_buf, strlen("Alfa" BODY));
}

ZTEST(storage_card, test_carga_do_tamanho_do_buffer)
{
	fill_pattern(big, STORAGE_FILE_MAX, 0x5AU);
	put_file("CHEIO.TXT", big, STORAGE_FILE_MAX);
	scan_expecting(1);

	/* O teto é inclusivo, e o arquivo cheio ocupa oito aglomerados neste
	 * volume: a carga atravessa a cadeia da FAT, não um setor só.
	 */
	zassert_equal((int)STORAGE_FILE_MAX, storage_load(0, track_buf, sizeof(track_buf)));
	zassert_mem_equal(big, track_buf, STORAGE_FILE_MAX);
}

ZTEST(storage_card, test_carga_de_arquivo_fragmentado)
{
	/* Um aglomerado de cada vez, alternando: é como fica um cartão em que
	 * arquivos foram copiados e apagados ao longo do tempo.
	 */
	static char other[STORAGE_FILE_MAX];
	struct fs_statvfs stat;
	struct fs_file_t a;
	struct fs_file_t b;
	size_t chunk;

	zassert_ok(fs_statvfs(CARD_MOUNT_POINT, &stat));
	chunk = stat.f_frsize;
	zassert_true(chunk < STORAGE_FILE_MAX,
		     "aglomerado de %zu B: o arquivo nao teria como fragmentar", chunk);

	fill_pattern(big, STORAGE_FILE_MAX, 0x11U);
	fill_pattern(other, sizeof(other), 0xEEU);

	fs_file_t_init(&a);
	fs_file_t_init(&b);
	zassert_ok(fs_open(&a, CARD_MOUNT_POINT "/FRAG1.TXT", FS_O_CREATE | FS_O_WRITE));
	zassert_ok(fs_open(&b, CARD_MOUNT_POINT "/FRAG2.TXT", FS_O_CREATE | FS_O_WRITE));

	for (size_t off = 0U; off < STORAGE_FILE_MAX; off += chunk) {
		zassert_equal((ssize_t)chunk, fs_write(&a, &big[off], chunk));
		zassert_equal((ssize_t)chunk, fs_write(&b, &other[off], chunk));
	}

	zassert_ok(fs_close(&a));
	zassert_ok(fs_close(&b));

	scan_expecting(2);

	zassert_equal((int)STORAGE_FILE_MAX, storage_load(0, track_buf, sizeof(track_buf)));
	zassert_mem_equal(big, track_buf, STORAGE_FILE_MAX, "o primeiro saiu embaralhado");

	zassert_equal((int)STORAGE_FILE_MAX, storage_load(1, track_buf, sizeof(track_buf)));
	zassert_mem_equal(other, track_buf, sizeof(other), "o segundo saiu embaralhado");
}

ZTEST(storage_card, test_carga_de_arquivo_vazio)
{
	put_file("NADA.TXT", "", 0U);
	scan_expecting(1);

	/* Zero bytes não é erro de carga: o arquivo carregou, e é o
	 * interpretador quem vai recusá-lo. A varredura já o marcou com `!`.
	 */
	zassert_equal(0, storage_load(0, track_buf, sizeof(track_buf)));
}

/* -------------------------------------------------------------------------
 * Carga: o arquivo grande é recusado, nunca truncado
 * ------------------------------------------------------------------------- */

ZTEST(storage_card, test_arquivo_maior_que_o_buffer_e_recusado)
{
	(void)memset(big, 'c', sizeof(big));
	(void)memcpy(big, "Grande" BODY, strlen("Grande" BODY));
	put_file("GRANDE.TXT", big, STORAGE_FILE_MAX + 1U);
	scan_expecting(1);

	/* Um byte além do teto. Meia melodia sem aviso seria pior que a tela
	 * de erro, e a tela de erro tem o que dizer.
	 */
	mark_track_buf();
	zassert_equal(-EFBIG, storage_load(0, track_buf, sizeof(track_buf)));
	assert_track_buf_untouched();

	zassert_str_equal("Maior que 4 KB", storage_error_text(-EFBIG));
}

ZTEST(storage_card, test_arquivo_maior_que_o_buffer_do_chamador_e_recusado)
{
	uint8_t small[16];

	put_text("FURELIS.TXT", FUR_ELISE);
	scan_expecting(1);

	(void)memset(small, UNTOUCHED, sizeof(small));
	zassert_equal(-EFBIG, storage_load(0, small, sizeof(small)));

	for (size_t i = 0U; i < sizeof(small); i++) {
		zassert_equal(UNTOUCHED, small[i], "byte %zu escrito numa carga recusada", i);
	}
}

ZTEST(storage_card, test_buffer_maior_nao_levanta_o_teto)
{
	static uint8_t roomy[2U * STORAGE_FILE_MAX];

	(void)memset(big, 'c', sizeof(big));
	(void)memcpy(big, "Grande" BODY, strlen("Grande" BODY));
	put_file("GRANDE.TXT", big, STORAGE_FILE_MAX + 1U);
	scan_expecting(1);

	/* A varredura listou a faixa com `!` por passar do teto. Um chamador
	 * com buffer folgado não pode fazê-la tocar mesmo assim: a lista e a
	 * carga têm de concordar.
	 */
	zassert_equal(-EFBIG, storage_load(0, roomy, sizeof(roomy)));
}

/* -------------------------------------------------------------------------
 * Carga: depois dela, o cartão não é mais necessário
 * ------------------------------------------------------------------------- */

/**
 * @brief Percorre a faixa em RAM como o interpretador vai percorrer.
 *
 * Do byte zero ao último, contando as notas do corpo — o que vem depois do
 * segundo `:`. Não é o interpretador, que é outra issue; é a prova de que
 * tudo de que ele vai precisar está no buffer.
 */
static size_t count_notes(const uint8_t *buf, size_t len)
{
	size_t colons = 0U;
	size_t notes = 0U;

	for (size_t i = 0U; i < len; i++) {
		if (buf[i] == ':') {
			colons++;
		} else if ((colons == 2U) && (buf[i] == ',')) {
			notes++;
		}
	}

	/* A última nota não tem vírgula depois. */
	return (colons == 2U) ? notes + 1U : 0U;
}

ZTEST(storage_card, test_reproducao_nao_toca_no_cartao)
{
	unsigned int reads;
	int size;

	put_text("FURELIS.TXT", FUR_ELISE);
	scan_expecting(1);

	size = storage_load(0, track_buf, sizeof(track_buf));
	zassert_equal((int)strlen(FUR_ELISE), size);

	reads = fake_disk_reads();

	/* O cartão sai na primeira nota. A carga já devolveu o volume: se
	 * tivesse deixado o empréstimo ou um arquivo aberto, o monitor não
	 * veria o soquete vazio.
	 */
	fake_disk_insert(false);
	zassert_equal(CARD_ABSENT, card_refresh(), "a carga deixou o volume emprestado");

	/* Tocar e, no fim, reiniciar: duas passagens do byte zero, sem o
	 * cartão.
	 */
	zassert_equal(40U, count_notes(track_buf, (size_t)size));
	zassert_equal(40U, count_notes(track_buf, (size_t)size), "reiniciar mudou a faixa");

	zassert_equal(reads, fake_disk_reads(), "a reproducao leu %u setor(es) do cartao",
		      fake_disk_reads() - reads);
	zassert_mem_equal(FUR_ELISE, track_buf, (size_t)size,
			  "tirar o cartao mexeu na faixa carregada");
}

ZTEST(storage_card, test_cargas_seguidas_nao_deixam_arquivo_aberto)
{
	put_text("FURELIS.TXT", FUR_ELISE);
	scan_expecting(1);

	/* O FatFs tem CONFIG_FS_FATFS_NUM_FILES objetos de arquivo, alocados
	 * de antemão. Uma carga que esquecesse o seu aberto esgotaria a
	 * reserva em poucas trocas de faixa, e "seguinte" pararia de
	 * funcionar sem motivo aparente.
	 */
	for (int i = 0; i < (4 * CONFIG_FS_FATFS_NUM_FILES); i++) {
		zassert_equal((int)strlen(FUR_ELISE), storage_load(0, track_buf, sizeof(track_buf)),
			      "a carga %d falhou", i);
	}
}

/* -------------------------------------------------------------------------
 * Carga: erros do contrato
 * ------------------------------------------------------------------------- */

ZTEST(storage_card, test_carga_recusa_argumentos_invalidos)
{
	uint8_t buf[16];

	zassert_equal(-EINVAL, storage_load(0, NULL, sizeof(buf)));
	zassert_equal(-EINVAL, storage_load(0, buf, 0U));
}

ZTEST(storage_card, test_carga_sem_cartao)
{
	put_text("FURELIS.TXT", FUR_ELISE);
	scan_expecting(1);

	fake_disk_insert(false);
	zassert_equal(CARD_ABSENT, card_refresh());

	mark_track_buf();
	zassert_equal(-ENODEV, storage_load(0, track_buf, sizeof(track_buf)));
	assert_track_buf_untouched();
}

ZTEST(storage_card, test_carga_com_volume_ilegivel)
{
	fake_disk_insert(false);
	zassert_equal(CARD_ABSENT, card_refresh());
	fake_disk_fail_read(true);
	fake_disk_insert(true);
	zassert_equal(CARD_UNREADABLE, card_refresh());

	zassert_equal(-EIO, storage_load(0, track_buf, sizeof(track_buf)));
}

ZTEST(storage_card, test_carga_fora_da_biblioteca)
{
	put_numbered_tracks(2U);
	scan_expecting(2);

	zassert_equal(-ENOENT, storage_load(-1, track_buf, sizeof(track_buf)));
	zassert_equal(-ENOENT, storage_load(2, track_buf, sizeof(track_buf)));
}

ZTEST(storage_card, test_carga_de_arquivo_que_sumiu)
{
	put_text("FURELIS.TXT", FUR_ELISE);
	scan_expecting(1);

	/* O cartão trocado por outro antes de a biblioteca ser varrida de
	 * novo: o índice existe, o arquivo não.
	 */
	zassert_ok(fs_unlink(CARD_MOUNT_POINT "/FURELIS.TXT"));

	zassert_equal(-ENOENT, storage_load(0, track_buf, sizeof(track_buf)));
}

ZTEST(storage_card, test_carga_com_cartao_que_para_de_ler)
{
	put_text("FURELIS.TXT", FUR_ELISE);
	scan_expecting(1);

	fake_disk_fail_read(true);

	zassert_equal(-EIO, storage_load(0, track_buf, sizeof(track_buf)));
}

ZTEST(storage_card, test_remocao_em_qualquer_ponto_da_carga)
{
	unsigned int accesses;
	int result = -EIO;

	/* O arquivo cheio, para a carga ter o maior número possível de
	 * pontos onde ser interrompida.
	 */
	fill_pattern(big, STORAGE_FILE_MAX, 0x3CU);
	put_file("CHEIO.TXT", big, STORAGE_FILE_MAX);
	scan_expecting(1);

	/* A mesma técnica da varredura interrompida: o cartão sai depois de
	 * 0, 1, 2... acessos, até a carga passar inteira. Remontar entre uma
	 * tentativa e outra esvazia o cache do FatFs, então cada tentativa
	 * começa fria e vai ao disco de novo.
	 */
	for (accesses = 0U; accesses < EJECT_SWEEP_MAX; accesses++) {
		fake_disk_eject_after(accesses);
		result = storage_load(0, track_buf, sizeof(track_buf));

		if (result >= 0) {
			break;
		}

		zassert_equal(-EIO, result, "remocao apos %u acessos deu %d", accesses, result);

		reinsert();
	}

	zassert_equal((int)STORAGE_FILE_MAX, result, "a carga nunca chegou ao fim");
	zassert_true(accesses > 0U, "nenhum ponto de remocao foi exercitado");
	zassert_mem_equal(big, track_buf, STORAGE_FILE_MAX);
}

ZTEST(storage_card, test_varredura_nova_substitui_a_anterior)
{
	struct track_meta library[LIBRARY_MAX];
	uint8_t buf[16];

	put_numbered_tracks(2U);
	zassert_equal(2, storage_scan(library, ARRAY_SIZE(library), NULL));

	wipe_root();
	zassert_equal(0, storage_scan(library, ARRAY_SIZE(library), NULL));

	zassert_equal(-ENOENT, storage_load(0, buf, sizeof(buf)),
		      "o indice da biblioteca anterior continuou valendo");
}

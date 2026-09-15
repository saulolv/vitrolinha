/**
 * @file main.c
 * @brief Bancada do RNF03: quanto custa abrir e ler uma faixa do cartão.
 *
 * O RNF03 dá 100 ms entre o botão e o som, **incluindo seguinte e anterior**,
 * que exigem abrir e interpretar outro arquivo. Ler 400 bytes a 24 MHz é
 * trivial; o que não é óbvio é o custo do `fs_open` sobre FatFs em SPI. Esta
 * bancada responde isso e alimenta a decisão sobre pré-carregar as faixas
 * vizinhas (issue #8).
 *
 * ## O que é medido e o que é modelado
 *
 * No `native_sim` o disco é de mentira (@ref sim_card.h), e a separação é
 * esta:
 *
 * - **Medido, e transferível para a placa**: quantos setores o FatFs pede, em
 *   quantas chamadas, e de que tamanho cada uma. É software puro acima do
 *   `disk_access` — não depende de transporte nem de processador, só da
 *   geometria do volume, que o cartão de mentira reproduz.
 * - **Modelado**: quanto cada setor custa no barramento (@ref sd_cost.h). A
 *   contagem de bytes sai do código do Zephyr; só o tempo de acesso do cartão
 *   é parâmetro, e por isso o relatório traz uma varredura de sensibilidade em
 *   vez de um número só.
 *
 * Na placa o mesmo binário mede de verdade, com o mesmo `k_cycle_get_64()`, e
 * as colunas de setor ficam vazias — lá não há como instrumentar o disco.
 *
 * ## Por que `k_cycle_get_64`
 *
 * A versão de 32 bits dá a volta a cada 28,6 s a 150 MHz. Não seria problema
 * para uma abertura de arquivo, mas seria para a medição de 60 s de melodia da
 * E6, e usar duas ferramentas para a mesma grandeza convida a erro.
 */

#include "bench.h"
#include "sd_cost.h"
#include "sim_card.h"

#include <zephyr/fs/fs.h>
#include <zephyr/fs/fs_interface.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <ff.h>

#include <errno.h>
#include <string.h>

/*
 * Sair de verdade ao terminar.
 *
 * Quando `main` devolve, o Zephyr não encerra: o núcleo continua, a thread
 * ociosa roda e o processo do `native_sim` fica de pé para sempre — o que numa
 * bancada parece travamento. `posix_exit` é o jeito de o alvo simulado
 * devolver o controle ao terminal, com código de saída.
 */
#if defined(CONFIG_ARCH_POSIX)
#include <posix_board_if.h>
#define BENCH_EXIT(code) posix_exit(code)
#else
#define BENCH_EXIT(code) return (code)
#endif

/** @brief Nome 8.3 em maiúsculas, que é o que o `readdir` devolve sem LFN. */
#define NAME_MAX_LEN 13U

/** @brief Uma faixa encontrada na varredura. */
struct entry {
	char name[NAME_MAX_LEN];
	uint32_t size;
};

/** @brief O que uma janela de medição produziu. */
struct span {
	/** Microssegundos entre o início e o fim, por `k_cycle_get_64`. */
	uint64_t us;
	/** O que o disco viu. Zerado na placa. */
	struct sim_card_trace trace;
};

static FATFS fat;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat,
	.mnt_point = BENCH_MOUNT_POINT,
};

/** @brief Destino da carga. Estático e de tamanho fixo, como na aplicação. */
static uint8_t track_buf[BENCH_FILE_MAX];

static struct entry library[BENCH_TRACKS];
static uint32_t library_count;

static uint64_t span_start_cycles;

/** @brief Abre uma janela de medição. */
static void span_start(void)
{
	sim_card_trace_reset();
	span_start_cycles = k_cycle_get_64();
}

/** @brief Fecha a janela aberta por @ref span_start. */
static void span_stop(struct span *out)
{
	uint64_t elapsed = k_cycle_get_64() - span_start_cycles;

	out->us = k_cyc_to_us_floor64(elapsed);
	sim_card_trace_get(&out->trace);
}

/**
 * @brief Custo modelado de uma janela, para um tempo de acesso qualquer.
 *
 * Refaz a conta sobre a forma das chamadas registradas, e não sobre o total de
 * setores: uma chamada de oito setores usa CMD18 e paga um CMD12, oito
 * chamadas de um setor usam CMD17 e pagam oito CMD13. Somar setores apagaria
 * a diferença.
 *
 * @param trace   Trilha da janela.
 * @param poll_ns Tempo de acesso do cartão por bloco.
 * @return Microssegundos.
 */
static uint64_t span_model_us(const struct sim_card_trace *trace, uint32_t poll_ns)
{
	uint32_t calls = MIN(trace->calls, SIM_CARD_TRACE_MAX);
	uint64_t ns = 0U;

	for (uint32_t i = 0U; i < calls; i++) {
		ns += sd_cost_read_ns(trace->blocks[i], poll_ns);
	}

	return ns / 1000U;
}

/** @brief Imprime as colunas de disco, ou travessões quando não há disco. */
static void print_disk_columns(const struct span *span)
{
	if (sim_card_available()) {
		printk("%8u %8u", span->trace.calls, span->trace.sectors);
	} else {
		printk("%8s %8s", "-", "-");
	}
}

/**
 * @brief Mostra que a execução de código não gasta tempo simulado.
 *
 * Não é curiosidade: é a razão de esta bancada contar setores em vez de
 * cronometrar o FatFs. No `native_sim` o relógio só anda quando a CPU é
 * parada de propósito, então cronometrar trabalho de processador devolve
 * zero — e um zero desses, tomado por medição, seria pior do que não medir.
 *
 * @see zephyr/boards/native/native_sim/cpu_wait.c
 */
static void report_clock(void)
{
	volatile uint64_t sink = 0U;
	uint64_t t0, work_us, wait_us;

	printk("\n[1] O relogio deste alvo\n");
	printk("    k_cycle_get_64, %u Hz\n", (unsigned int)sys_clock_hw_cycles_per_sec());

	t0 = k_cycle_get_64();
	for (uint32_t i = 0U; i < 10000000U; i++) {
		sink = sink + i;
	}
	work_us = k_cyc_to_us_floor64(k_cycle_get_64() - t0);

	t0 = k_cycle_get_64();
	k_busy_wait(1000);
	wait_us = k_cyc_to_us_floor64(k_cycle_get_64() - t0);

	printk("    10 milhoes de somas ....... %8llu us\n", (unsigned long long)work_us);
	printk("    k_busy_wait(1000) ......... %8llu us\n", (unsigned long long)wait_us);

	if (work_us == 0U) {
		printk("    -> execucao nao gasta tempo simulado; o que vale aqui e a\n");
		printk("       contagem de setores, nao o cronometro de trabalho de CPU\n");
	}
}

/** @brief Mostra o modelo de custo com que o resto do relatório é lido. */
static void report_model(void)
{
	printk("\n[2] Modelo de custo do setor (sd_cost.h)\n");
	printk("    barramento .................... %u Hz\n", SD_COST_CLOCK_HZ);
	printk("    1 setor  (CMD17) .............. %4llu B = %4llu us de barramento\n",
	       (unsigned long long)sd_cost_bytes(1U),
	       (unsigned long long)(sd_cost_read_ns(1U, 0U) / 1000U));
	printk("    8 setores (CMD18) ............. %4llu B = %4llu us de barramento\n",
	       (unsigned long long)sd_cost_bytes(8U),
	       (unsigned long long)(sd_cost_read_ns(8U, 0U) / 1000U));
	printk("    acesso do cartao, por bloco ... %u us (parametro; varrido em [6])\n",
	       SD_COST_DEFAULT_POLL_NS / 1000U);
}

/**
 * @brief Varre a biblioteca como o `storage` vai varrer.
 *
 * `opendir`, `readdir` até o fim e, para cada entrada, abrir e ler os
 * primeiros bytes — porque o nome exibido sai do cabeçalho RTTTL, não do nome
 * de arquivo 8.3 que o `readdir` devolve.
 */
static int scan_library(struct span *span)
{
	struct fs_dir_t dir;
	int err;

	fs_dir_t_init(&dir);
	library_count = 0U;

	span_start();

	err = fs_opendir(&dir, BENCH_MOUNT_POINT);
	if (err != 0) {
		return err;
	}

	while (library_count < ARRAY_SIZE(library)) {
		struct fs_dirent ent;
		struct fs_file_t file;
		char path[64];
		uint8_t header[BENCH_HEADER_BYTES];

		err = fs_readdir(&dir, &ent);
		if (err != 0 || ent.name[0] == '\0') {
			break;
		}

		if (ent.type != FS_DIR_ENTRY_FILE) {
			continue;
		}

		(void)snprintk(path, sizeof(path), BENCH_MOUNT_POINT "/%s", ent.name);

		fs_file_t_init(&file);
		if (fs_open(&file, path, FS_O_READ) == 0) {
			(void)fs_read(&file, header, sizeof(header));
			(void)fs_close(&file);
		}

		(void)strncpy(library[library_count].name, ent.name, NAME_MAX_LEN - 1U);
		library[library_count].name[NAME_MAX_LEN - 1U] = '\0';
		library[library_count].size = ent.size;
		library_count++;
	}

	(void)fs_closedir(&dir);
	span_stop(span);

	return err;
}

/**
 * @brief Carrega a faixa inteira, como o `storage_load` vai carregar.
 *
 * @param index Posição na biblioteca varrida.
 * @param span  Recebe a medição.
 * @return Bytes lidos, ou erro negativo.
 */
static int load_track(uint32_t index, struct span *span)
{
	struct fs_file_t file;
	char path[64];
	int read;
	int err;

	(void)snprintk(path, sizeof(path), BENCH_MOUNT_POINT "/%s", library[index].name);

	fs_file_t_init(&file);

	span_start();

	err = fs_open(&file, path, FS_O_READ);
	if (err != 0) {
		span_stop(span);
		return err;
	}

	read = fs_read(&file, track_buf, sizeof(track_buf));
	err = fs_close(&file);

	span_stop(span);

	return (read < 0) ? read : ((err < 0) ? err : read);
}

/**
 * @brief Varre o tempo de acesso do cartão e diz onde o orçamento quebra.
 *
 * O ponto da varredura é não depender do parâmetro: em vez de afirmar "custa
 * X", ela diz a partir de que custo de acesso a decisão mudaria. Se esse
 * limiar estiver longe de qualquer cartão plausível, a decisão está tomada
 * mesmo sem o número de bancada.
 */
static void report_sensitivity(const struct span *worst)
{
	static const uint32_t poll_us[] = {0, 100, 250, 500, 1000, 2500, 5000, 10000, 25000};
	uint32_t lo = 0U;
	uint32_t hi = 100000U;

	printk("\n[6] Sensibilidade ao tempo de acesso do cartao\n");

	if (!sim_card_available()) {
		/* Na placa não há o que varrer: o tempo acima é medido, não
		 * modelado, e o cartão já respondeu o que tinha a responder.
		 */
		printk("    Nao se aplica: aqui o tempo e medido, nao modelado.\n");
		return;
	}

	printk("    Orcamento do armazenamento: %u us"
	       " (%u total - %u antirrebote - %u reserva do interpretador)\n",
	       BENCH_STORAGE_BUDGET_US, BENCH_RNF03_TOTAL_US, BENCH_DEBOUNCE_US,
	       BENCH_PARSER_RESERVE_US);
	printk("\n    acesso/bloco   pior carga   veredito\n");

	for (size_t i = 0U; i < ARRAY_SIZE(poll_us); i++) {
		uint64_t us = span_model_us(&worst->trace, poll_us[i] * 1000U);

		printk("    %9u us %9llu us   %s\n", poll_us[i], (unsigned long long)us,
		       (us <= BENCH_STORAGE_BUDGET_US) ? "cabe" : "ESTOURA");
	}

	if (span_model_us(&worst->trace, 0U) > BENCH_STORAGE_BUDGET_US) {
		printk("\n    O orcamento ja estoura com acesso instantaneo: o gargalo e o\n");
		printk("    barramento, e nenhum cartao o conserta.\n");
		return;
	}

	/* Busca binária do maior acesso por bloco que ainda cabe. */
	while (lo < hi) {
		uint32_t mid = lo + (hi - lo + 1U) / 2U;

		if (span_model_us(&worst->trace, mid * 1000U) <= BENCH_STORAGE_BUDGET_US) {
			lo = mid;
		} else {
			hi = mid - 1U;
		}
	}

	printk("\n    O orcamento so quebra se o cartao levar mais de %u us por bloco,\n", lo);
	printk("    contra %u us de TAAC que a especificacao fixa para todo SDHC.\n",
	       SD_COST_DEFAULT_POLL_NS / 1000U);
}

/**
 * @brief Fecha o relatório com a decisão e com o que ele não viu.
 *
 * O critério da issue #8 é "existe um número medido e a decisão de
 * pré-carregar está tomada com base nele". A decisão sai impressa aqui, junto
 * com a ressalva, para que ninguém leia a tabela sem ler a ressalva.
 */
static void report_verdict(const struct span *worst)
{
	bool fits = worst->us <= BENCH_STORAGE_BUDGET_US;

	printk("\n[7] Veredito\n");
	printk("    Pior carga: %llu us, contra %u us de orcamento. Folga de %lld us.\n",
	       (unsigned long long)worst->us, BENCH_STORAGE_BUDGET_US,
	       (long long)BENCH_STORAGE_BUDGET_US - (long long)worst->us);

	if (sim_card_available()) {
		printk("\n    O que aqui e MEDIDO: %u setores em %u chamadas. Isso e software\n",
		       worst->trace.sectors, worst->trace.calls);
		printk("    puro acima do disco e vale igual na placa.\n");
		printk("    O que aqui e MODELADO: o tempo. Ver sd_cost.h e a varredura [6].\n");
	} else {
		printk("\n    Medicao de verdade, na placa, com relogio de verdade.\n");
	}

	printk("\n    Decisao: %s pre-carregar as faixas vizinhas.\n",
	       fits ? "NAO" : "E PRECISO");

	printk("\n    Ressalva que nenhum modelo alcanca: se a resposta R1 do cartao nao\n");
	printk("    couber nos 9 bytes do pacote de comando, o sdhc_spi cai num laco de\n");
	printk("    k_msleep(10) POR COMANDO (sdhc_spi.c:253), e sao dois comandos por\n");
	printk("    chamada de leitura. Um unico atraso desses custa 10 ms de uma vez.\n");
	printk("    So medindo na placa, com este mesmo binario, para descartar.\n");
	printk("\n");
}

int main(void)
{
	struct span mount_span;
	struct span scan_span;
	struct span cold_span;
	struct span worst = {0};
	uint32_t worst_index = 0U;
	int err;

	printk("\n=====================================================================\n");
	printk("Vitrolinha - bancada do RNF03: custo de abrir e ler uma faixa\n");
	printk("Alvo: " CONFIG_BOARD_TARGET "\n");
	printk("=====================================================================\n");

	report_clock();
	report_model();

	sim_card_arm(false);
	err = sim_card_setup(BENCH_TRACKS);
	if (err != 0) {
		printk("\nFALHA ao preparar o cartao simulado: %d\n", err);
		BENCH_EXIT(err);
	}
	sim_card_arm(true);

	span_start();
	err = fs_mount(&mp);
	span_stop(&mount_span);
	if (err != 0) {
		printk("\nFALHA ao montar %s: %d\n", BENCH_MOUNT_POINT, err);
		BENCH_EXIT(err);
	}

	printk("\n[3] Montagem fria\n");
	printk("    %-28s %8s %8s %10s\n", "operacao", "chamadas", "setores", "us");
	printk("    %-28s ", "fs_mount");
	print_disk_columns(&mount_span);
	printk(" %9llu\n", (unsigned long long)mount_span.us);

	err = scan_library(&scan_span);
	if (err != 0 || library_count == 0U) {
		printk("\nFALHA na varredura: %d, %u faixas\n", err, library_count);
		BENCH_EXIT((err != 0) ? err : -ENOENT);
	}

	printk("\n[4] Varredura da biblioteca (%u faixas: opendir, readdir, cabecalho)\n",
	       library_count);
	printk("    %-28s ", "scan");
	print_disk_columns(&scan_span);
	printk(" %9llu\n", (unsigned long long)scan_span.us);
	printk("    Fora do caminho do RNF03: acontece uma vez, na montagem.\n");

	printk("\n[5] Carga de faixa - o caminho do RNF03\n");
	printk("    %-14s %8s %8s %8s %10s\n", "faixa", "bytes", "chamadas", "setores", "us");

	for (uint32_t i = 0U; i < library_count; i++) {
		struct span span;
		int size = load_track(i, &span);

		if (size < 0) {
			printk("    %-14s carga falhou: %d\n", library[i].name, size);
			continue;
		}

		printk("    %-14s %8d ", library[i].name, size);
		print_disk_columns(&span);
		printk(" %9llu\n", (unsigned long long)span.us);

		if (span.us >= worst.us) {
			worst = span;
			worst_index = i;
		}
	}

	/* Desmontar e montar de novo esvazia a janela de setor do FatFs, que
	 * guarda o último setor de diretório lido. É o pior caso de verdade:
	 * a primeira faixa aberta depois de o cartão entrar.
	 */
	(void)fs_unmount(&mp);
	(void)fs_mount(&mp);
	(void)load_track(worst_index, &cold_span);

	printk("\n    Pior faixa (%s), com a janela do FatFs fria:\n", library[worst_index].name);
	printk("    %-14s %8s ", library[worst_index].name, "-");
	print_disk_columns(&cold_span);
	printk(" %9llu\n", (unsigned long long)cold_span.us);

	if (cold_span.us > worst.us) {
		worst = cold_span;
	}

	report_sensitivity(&worst);

	report_verdict(&worst);

	BENCH_EXIT(0);
}

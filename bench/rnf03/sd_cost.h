/**
 * @file sd_cost.h
 * @brief Quanto custa, em bytes de SPI, ler setores do microSD.
 *
 * O simulador conta com exatidão **quantos setores** o FatFs pede para cada
 * operação — isso é software puro acima do `disk_access`, e não depende de
 * transporte nem de processador. O que ele não pode saber é **quanto tempo**
 * cada setor custa no barramento da placa. É o que este arquivo fornece.
 *
 * As contagens de bytes abaixo não são estimativas: saem da leitura do
 * caminho de código que o Zephyr percorre a cada `disk_access_read`, com
 * arquivo e linha citados. O único termo que **não** é dedutível do código é
 * o tempo que o cartão leva para começar a devolver dados — ver
 * ::SD_COST_DEFAULT_POLL_NS.
 *
 * @see docs/medicoes/rnf03-abertura-de-faixa.md
 */

#ifndef VITROLINHA_BENCH_SD_COST_H_
#define VITROLINHA_BENCH_SD_COST_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Relógio do barramento, em hertz.
 *
 * `spi-max-frequency = <24000000>` no nó `sdhc0` da ZBook P2. O cartão
 * negocia para baixo se não sustentar, mas qualquer SD de velocidade padrão
 * aceita 25 MHz, então 24 MHz é o que vale.
 */
#define SD_COST_CLOCK_HZ 24000000U

/**
 * @brief Bytes do pacote de comando quando há dados em seguida.
 *
 * `sdhc_spi_send_cmd(..., data_present=true)` faz **uma** transação de
 * `SD_SPI_CMD_SIZE + 3` bytes: um 0xFF de prontidão, seis de comando e dois
 * de folga para a resposta R1 chegar.
 *
 * @see zephyr/drivers/sdhc/sdhc_spi.c:392
 */
#define SD_COST_CMD_BYTES 9U

/**
 * @brief Bytes do pacote de comando quando não há dados.
 *
 * Sem dados a seguir o driver pode gastar ciclos à vontade, e transmite o
 * buffer inteiro de uma vez: `sizeof(scratch)`, que é `MAX_CMD_READ`.
 *
 * @see zephyr/drivers/sdhc/sdhc_spi.c:21 e 381
 */
#define SD_COST_CMDONLY_BYTES 21U

/** @brief Um bloco de dados. */
#define SD_COST_BLOCK_BYTES 512U

/**
 * @brief CRC16 mais o byte de encerramento, por bloco.
 *
 * @see zephyr/drivers/sdhc/sdhc_spi.c:611 (`SD_SPI_CRC16_SIZE + 1`)
 */
#define SD_COST_CRC_BYTES 3U

/**
 * @brief Sondagem de ocupação, em `sdmmc_wait_ready`.
 *
 * `sdhc_card_busy` lê exatamente um byte e decide pelo valor.
 *
 * @see zephyr/drivers/sdhc/sdhc_spi.c:180
 */
#define SD_COST_BUSY_BYTES 1U

/**
 * @brief O CMD13 que o subsistema SD manda **depois de toda leitura**.
 *
 * Este é o custo que passa despercebido: `card_read` não termina no dado.
 * Ele chama `sdmmc_wait_ready`, que sonda a ocupação e emite um
 * SEND_STATUS completo antes de devolver o controle. São 22 bytes a mais
 * por chamada de leitura, dado nenhum.
 *
 * @see zephyr/subsys/sd/sd_ops.c:534 e 20
 */
#define SD_COST_STATUS_BYTES SD_COST_CMDONLY_BYTES

/**
 * @brief O CMD12 que encerra uma leitura de múltiplos blocos.
 *
 * Só aparece quando `num_blocks > 1`, porque aí o comando é CMD18 e o
 * driver é obrigado a mandar STOP_TRANSMISSION.
 *
 * @see zephyr/drivers/sdhc/sdhc_spi.c:668
 */
#define SD_COST_STOP_BYTES SD_COST_CMDONLY_BYTES

/**
 * @brief Tempo de acesso do cartão por bloco, em nanossegundos.
 *
 * **Este é o único número aqui que não sai de código.** Entre o comando e o
 * primeiro byte de dado o cartão fica calado, e `sdhc_skip` sonda o
 * barramento **um byte por transação de SPI** até o token 0xFE aparecer
 * (zephyr/drivers/sdhc/sdhc_spi.c:431). Quanto dura essa espera depende do
 * cartão.
 *
 * O padrão de 1 ms é o TAAC que a especificação física do SD **fixa** para
 * CSD versão 2.0, isto é, para todo cartão SDHC — não é média de medição
 * nem folclore. É um teto típico, não o pior caso: a mesma especificação
 * admite até 100 ms de tempo limite de leitura.
 *
 * Por isso o relatório não depende deste valor: a varredura de
 * sensibilidade mostra o veredito para uma faixa inteira de valores, e o
 * número de bancada entra no lugar quando a placa existir.
 */
#define SD_COST_DEFAULT_POLL_NS 1000000U

/**
 * @brief Bytes que trafegam numa chamada de leitura de @p blocks setores.
 *
 * Um único `disk_access_read` de N setores vira, no barramento:
 * CMD17 (N==1) ou CMD18 (N>1), N blocos com CRC, o CMD12 de encerramento
 * quando houve CMD18, e o par sondagem + CMD13 do `sdmmc_wait_ready`.
 *
 * @param blocks Setores pedidos na chamada. Zero devolve zero.
 * @return Total de bytes trocados no barramento.
 */
static inline uint64_t sd_cost_bytes(uint32_t blocks)
{
	uint64_t bytes;

	if (blocks == 0U) {
		return 0U;
	}

	bytes = SD_COST_CMD_BYTES;
	bytes += (uint64_t)blocks * (SD_COST_BLOCK_BYTES + SD_COST_CRC_BYTES);
	bytes += SD_COST_BUSY_BYTES + SD_COST_STATUS_BYTES;

	if (blocks > 1U) {
		bytes += SD_COST_STOP_BYTES;
	}

	return bytes;
}

/**
 * @brief Duração de uma chamada de leitura de @p blocks setores.
 *
 * @param blocks  Setores pedidos na chamada.
 * @param poll_ns Tempo de acesso do cartão por bloco. Ver
 *                ::SD_COST_DEFAULT_POLL_NS.
 * @return Nanossegundos.
 */
static inline uint64_t sd_cost_read_ns(uint32_t blocks, uint32_t poll_ns)
{
	uint64_t ns;

	ns = sd_cost_bytes(blocks) * 8ULL * 1000000000ULL / SD_COST_CLOCK_HZ;
	ns += (uint64_t)blocks * poll_ns;

	return ns;
}

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_BENCH_SD_COST_H_ */

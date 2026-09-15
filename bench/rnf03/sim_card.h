/**
 * @file sim_card.h
 * @brief Cartão microSD de mentira, esparso e instrumentado, para o `native_sim`.
 *
 * Registra um disco no `disk_access` com a **geometria de um cartão de
 * verdade** — 8 GiB, FAT32, aglomerados de 32 KiB —, conta cada setor que o
 * FatFs pede e cobra, no relógio simulado, o tempo que aquele acesso custaria
 * no barramento SPI da placa (ver @ref sd_cost.h).
 *
 * A geometria é o ponto. O padrão de acesso do FatFs depende do tamanho do
 * aglomerado, da largura da entrada da FAT e do desenho do diretório raiz —
 * não do transporte. Um disco de RAM pequeno sairia FAT12 com aglomerados de
 * 512 B e produziria uma contagem de setores que não vale para o cartão.
 *
 * O armazenamento é **esparso**: só os setores efetivamente escritos ocupam
 * memória. Formatar 8 GiB toca ~4200 setores — as duas FATs e o aglomerado da
 * raiz —, e é por isso que um cartão de 8 GiB cabe em poucos megabytes de
 * processo.
 *
 * Fora do `native_sim` tudo aqui vira função vazia: na placa quem responde é
 * o cartão.
 */

#ifndef VITROLINHA_BENCH_SIM_CARD_H_
#define VITROLINHA_BENCH_SIM_CARD_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Nome do disco, igual ao `disk-name` do nó `sdhc0` da placa. */
#define SIM_CARD_DISK_NAME "SD"

/** @brief Chamadas de leitura registradas por janela de medição. */
#define SIM_CARD_TRACE_MAX 256U

/**
 * @brief O que o disco viu durante uma janela de medição.
 *
 * `blocks[i]` é o `num_sector` da i-ésima chamada de leitura, e é disso que
 * sai o custo modelado: uma chamada de 8 setores não custa o mesmo que oito
 * chamadas de 1, porque o FatFs usa CMD18 numa e CMD17 nas outras.
 */
struct sim_card_trace {
	/** Chamadas de leitura. */
	uint32_t calls;
	/** Setores lidos, somando todas as chamadas. */
	uint32_t sectors;
	/** Chamadas que não couberam em @ref blocks. */
	uint32_t dropped;
	/** Setores por chamada. */
	uint32_t blocks[SIM_CARD_TRACE_MAX];
	/** Primeiro setor de cada chamada, para ver a localidade. */
	uint32_t start[SIM_CARD_TRACE_MAX];
};

#if defined(CONFIG_BOARD_NATIVE_SIM)

/**
 * @brief Registra o disco, formata em FAT32 e escreve a biblioteca.
 *
 * Ao voltar, o volume está desmontado: quem mede quer cronometrar a montagem
 * fria, não herdá-la pronta.
 *
 * @param tracks Faixas a criar, com tamanhos de 128 B a
 *               `128 * tracks` bytes.
 * @return 0, ou erro negativo de `fs_*`.
 */
int sim_card_setup(uint32_t tracks);

/**
 * @brief Liga ou desliga a cobrança de tempo no relógio simulado.
 *
 * Desligada durante a formatação e a escrita da biblioteca, que não fazem
 * parte de medição nenhuma.
 */
void sim_card_arm(bool on);

/** @brief Define o tempo de acesso por bloco. Ver ::SD_COST_DEFAULT_POLL_NS. */
void sim_card_set_poll_ns(uint32_t poll_ns);

/** @brief Zera a trilha e começa uma janela de medição. */
void sim_card_trace_reset(void);

/** @brief Copia a trilha da janela corrente. */
void sim_card_trace_get(struct sim_card_trace *out);

/** @brief Verdadeiro se este alvo instrumenta o disco. */
static inline bool sim_card_available(void)
{
	return true;
}

#else /* placa */

static inline int sim_card_setup(uint32_t tracks)
{
	ARG_UNUSED(tracks);
	return 0;
}

static inline void sim_card_arm(bool on)
{
	ARG_UNUSED(on);
}

static inline void sim_card_set_poll_ns(uint32_t poll_ns)
{
	ARG_UNUSED(poll_ns);
}

static inline void sim_card_trace_reset(void)
{
}

static inline void sim_card_trace_get(struct sim_card_trace *out)
{
	*out = (struct sim_card_trace){0};
}

static inline bool sim_card_available(void)
{
	return false;
}

#endif /* CONFIG_BOARD_NATIVE_SIM */

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_BENCH_SIM_CARD_H_ */

/**
 * @file fake_disk.h
 * @brief Um cartão que o teste segura na mão: entra, sai, e falha sob encomenda.
 *
 * Registra no `disk_access` um disco chamado `"SD"` — o mesmo nome do nó
 * `sdhc0` da ZBook — respaldado por um vetor em RAM. A partir daí o módulo
 * `card` sob teste não tem como saber que não é um cartão: ele fala com o
 * `disk_access` e com o FatFs, exatamente como na placa.
 *
 * É isto que torna a issue #9 testável. "Inserir e remover o cartão é
 * detectado corretamente" é um critério sobre um evento físico; reproduzi-lo
 * na bancada depende de alguém puxar o cartão na hora certa, e não é
 * reproduzível em CI de jeito nenhum.
 *
 * O disco imita a máquina de estados do driver de verdade
 * (`zephyr/drivers/disk/sdmmc_subsys.c`): sem mídia devolve
 * `DISK_STATUS_NOMEDIA`, com mídia e sem inicializar devolve
 * `DISK_STATUS_UNINIT`, e só depois do `DISK_IOCTL_CTRL_INIT` devolve
 * `DISK_STATUS_OK`. Não é capricho — é essa transição que faz o FatFs
 * inicializar o cartão na montagem e liberá-lo na desmontagem, e é ela que o
 * `card` usa para descobrir que o soquete esvaziou.
 */

#ifndef VITROLINHA_TEST_FAKE_DISK_H_
#define VITROLINHA_TEST_FAKE_DISK_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registra o disco e formata o volume, com o soquete vazio.
 *
 * Formatar uma vez no início é o que permite ao teste ter um cartão "bom" —
 * o firmware nunca formata, e `CONFIG_FS_FATFS_MOUNT_MKFS` fica desligado
 * aqui como no firmware, justamente para que um volume ruim continue ruim.
 *
 * @return 0, ou erro negativo de `disk_access_register` ou `fs_mkfs`.
 */
int fake_disk_setup(void);

/**
 * @brief Liga ou desliga o disco do `disk_access`.
 *
 * Desligado, `disk_access_status` responde erro em vez de estado — que é o
 * que acontece quando o devicetree não tem nó `zephyr,sdmmc-disk` nenhum. É
 * exatamente a situação do alvo de simulação do firmware, e o módulo tem de
 * concluir "ausente" em vez de tratar como falha.
 */
void fake_disk_register(bool registered);

/**
 * @brief Põe ou tira o cartão do soquete.
 *
 * É o equivalente do pino de card-detect: mexe só no que
 * `disk_access_status` responde, sem apagar o conteúdo do volume — tirar e
 * pôr o mesmo cartão devolve o mesmo cartão.
 */
void fake_disk_insert(bool inserted);

/**
 * @brief Faz o cartão recusar a inicialização.
 *
 * Cartão presente cujo `CTRL_INIT` falha: meio físico inacessível. A
 * montagem nem chega a ler setor nenhum.
 */
void fake_disk_fail_init(bool failing);

/**
 * @brief Faz toda leitura falhar.
 *
 * Cartão que inicializa e depois não entrega setor: o `f_mount` quebra ao
 * ler o setor de arranque. É o caminho por onde o volume ilegível aparece
 * sem que o meio físico esteja ausente.
 */
void fake_disk_fail_read(bool failing);

/**
 * @brief Faz o `DISK_IOCTL_CTRL_DEINIT` falhar.
 *
 * Reproduz a desmontagem que não completa — o que a issue zephyr#94033
 * descreve na remoção a quente. Serve para conferir que o módulo segue em
 * frente em vez de ficar preso achando que o volume continua montado.
 */
void fake_disk_fail_deinit(bool failing);

/**
 * @brief Devolve o disco ao `disk_access`, esvazia o soquete e tira as falhas.
 *
 * Não desfaz a formatação: o volume continua válido.
 */
void fake_disk_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_TEST_FAKE_DISK_H_ */

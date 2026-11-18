/**
 * @file host_libc_fault.h
 * @brief Falhas da libc do computador sob encomenda, para o simulador.
 *
 * O storage_host e o loader falam com a libc do computador. Os erros que eles
 * tratam — `readdir`, `fstatat`, `fstat` e `read` falhando, ou o arquivo
 * encurtando entre o `fstat` e o `read` — não se provocam com arquivos de
 * verdade: o teste roda como root no contêiner, o que anula permissões, e
 * nada acontece entre duas chamadas de uma mesma função do módulo.
 *
 * Este arquivo define essas quatro funções dentro do binário do teste. No
 * native_sim o executável fica à frente da libc na resolução de símbolos,
 * então o código sob teste chama estas, que delegam à libc (pelo
 * `RTLD_NEXT`) a menos que uma falha tenha sido armada. Cada falha armada
 * vale para uma única chamada.
 */

#ifndef VITROLINHA_TEST_HOST_LIBC_FAULT_H_
#define VITROLINHA_TEST_HOST_LIBC_FAULT_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @brief O que a próxima chamada interceptada faz. */
enum host_libc_fault {
	/** @brief Nada: todas as chamadas vão à libc. */
	HOST_LIBC_FAULT_NONE = 0,
	/** @brief `readdir` devolve NULL com `errno` = EIO. */
	HOST_LIBC_FAULT_READDIR,
	/** @brief `fstatat` devolve -1 com `errno` = EIO. */
	HOST_LIBC_FAULT_FSTATAT,
	/** @brief `fstat` devolve -1 com `errno` = EIO. */
	HOST_LIBC_FAULT_FSTAT,
	/** @brief `read` devolve -1 com `errno` = EINTR, como sob um sinal. */
	HOST_LIBC_FAULT_READ_EINTR,
	/** @brief `read` devolve -1 com `errno` = EIO. */
	HOST_LIBC_FAULT_READ_EIO,
	/** @brief `read` devolve 0: o arquivo encurtou depois do `fstat`. */
	HOST_LIBC_FAULT_READ_EOF,
};

/**
 * @brief Arma uma falha para a próxima chamada da função correspondente.
 *
 * @param fault Falha a armar; ::HOST_LIBC_FAULT_NONE desarma.
 */
void host_libc_fault_arm(enum host_libc_fault fault);

/**
 * @brief Roda @p hook uma vez, na próxima chamada de `read`, antes de ler.
 *
 * Serve para encaixar uma ação no meio de uma carga — por exemplo, substituir
 * o pedido que o loader está atendendo enquanto ele lê o arquivo.
 *
 * @param hook Função a rodar; NULL cancela.
 */
void host_libc_fault_on_read(void (*hook)(void));

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_TEST_HOST_LIBC_FAULT_H_ */

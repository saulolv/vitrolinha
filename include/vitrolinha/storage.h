/**
 * @file storage.h
 * @brief Contrato: varredura e carga da biblioteca.
 *
 * Isola o cartão do resto do sistema. Nenhum outro módulo inclui cabeçalho
 * de sistema de arquivos, e nenhum outro módulo sabe que existe FAT, SPI ou
 * detecção de presença — o que é o que permite trocar a origem das faixas
 * (cartão, flash interna, dados embutidos no binário) sem tocar em quem as
 * consome.
 *
 * Montar o cartão **não** faz parte da interface, de propósito: a
 * implementação que fala com o hardware monta sozinha na inicialização, e o
 * chamador só descobre o resultado por @ref storage_present ou pelo código
 * de erro da varredura.
 *
 * @note Nada aqui pode ser chamado da thread `player` nem do callback do
 *       temporizador. É por isso que o arquivo é carregado inteiro antes de
 *       a reprodução começar: durante a música, este módulo não é tocado
 *       nenhuma vez.
 *
 * @see docs/especificacao-vitrolinha.md, seções 6.3 e 9
 */

#ifndef VITROLINHA_STORAGE_H_
#define VITROLINHA_STORAGE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <vitrolinha/track.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tamanho do buffer de faixa, em bytes.
 *
 * Um RTTTL típico tem de 100 a 400 bytes; 4 KB cobrem cerca de mil notas.
 * Arquivo maior é **rejeitado**, não truncado: meia melodia sem aviso é pior
 * do que uma mensagem de erro.
 */
#define STORAGE_FILE_MAX 4096U

/**
 * @brief Varre a biblioteca e preenche os metadados das faixas encontradas.
 *
 * @param out Vetor de destino, com pelo menos @p max posições.
 * @param max Capacidade de @p out. Valores acima de ::LIBRARY_MAX são
 *            tratados como ::LIBRARY_MAX.
 *
 * @return Quantidade de faixas escritas em @p out, de 0 a
 *         `min(max, LIBRARY_MAX)`, ou um erro negativo:
 * @retval -EINVAL @p out nulo ou @p max igual a zero.
 * @retval -ENODEV Sem cartão.
 * @retval -EIO    O cartão está presente mas não pôde ser lido.
 */
int storage_scan(struct track_meta *out, size_t max);

/**
 * @brief Carrega uma faixa inteira para a memória.
 *
 * A carga é integral e antecipada. É o que faz a reprodução não acessar o
 * cartão nenhuma vez, satisfaz o RNF06 por construção, torna o RNF07 quase
 * gratuito e reduz "reiniciar a faixa" a reinterpretar do byte zero.
 *
 * @param track Índice devolvido por @ref storage_scan.
 * @param buf   Destino, de tamanho fixo. Convém que seja ::STORAGE_FILE_MAX.
 * @param len   Capacidade de @p buf em bytes.
 *
 * @return Quantidade de bytes escritos em @p buf, ou um erro negativo:
 * @retval -EINVAL @p buf nulo ou @p len igual a zero.
 * @retval -ENOENT @p track fora da biblioteca.
 * @retval -ENODEV Sem cartão.
 * @retval -EFBIG  O arquivo não cabe em @p len. Nada foi escrito.
 * @retval -EIO    Falha de leitura.
 */
int storage_load(int track, uint8_t *buf, size_t len);

/**
 * @brief Diz se há cartão utilizável agora.
 *
 * @return Verdadeiro se a biblioteca pode ser varrida e lida.
 *
 * @note Falso durante a reprodução não interrompe a faixa: ela já está
 *       inteira em RAM (RNF07).
 */
bool storage_present(void);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_STORAGE_H_ */

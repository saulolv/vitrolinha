/**
 * @file storage.h
 * @brief Contrato: varredura e carga da biblioteca.
 *
 * Isola a origem dos arquivos do resto do sistema. Nenhum outro módulo inclui
 * cabeçalho de sistema de arquivos, nem sabe que existe FAT, SPI ou
 * detecção de presença — o que é o que permite trocar a origem das faixas
 * (cartão, pasta do computador, dados embutidos no binário) sem tocar em quem as
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
 * Limite comum às implementações, incluindo os CSVs no computador.
 * Arquivo maior é **rejeitado**, não truncado: meia melodia sem aviso é pior
 * do que uma mensagem de erro.
 */
#define STORAGE_FILE_MAX 4096U

/**
 * @brief Varre a biblioteca e preenche os metadados das faixas encontradas.
 *
 * As faixas saem na ordem em que a origem as guarda — no cartão, a ordem do
 * diretório, que é a ordem em que os arquivos foram copiados. É essa ordem
 * que dá sentido a "o 33.º arquivo é ignorado".
 *
 * Toda varredura substitui a biblioteca anterior: os índices passados a
 * @ref storage_load passam a valer para esta. Uma varredura que falha deixa a
 * biblioteca **vazia**, e @p out pode ter sido escrito em parte.
 *
 * @param out     Vetor de destino, com pelo menos @p max posições.
 * @param max     Capacidade de @p out. Valores acima de ::LIBRARY_MAX são
 *                tratados como ::LIBRARY_MAX.
 * @param skipped Se não nulo, recebe quantas faixas existiam mas não couberam
 *                em @p out — é o número que a tela mostra no aviso de
 *                biblioteca cheia. Zerado antes de qualquer retorno, inclusive
 *                de erro.
 *
 * @return Quantidade de faixas escritas em @p out, de 0 a
 *         `min(max, LIBRARY_MAX)`, ou um erro negativo:
 * @retval -EINVAL @p out nulo ou @p max igual a zero.
 * @retval -ENODEV Sem cartão (no `storage_host`, pasta ausente).
 * @retval -EIO    O cartão está presente mas não pôde ser lido.
 *
 * @warning **Bloqueia.** No cartão, abre cada arquivo para ler o cabeçalho, e
 *          pode esperar uma montagem em curso. Chamar de thread de prioridade
 *          baixa, nunca da `player`.
 */
int storage_scan(struct track_meta *out, size_t max, size_t *skipped);

/**
 * @brief Carrega uma faixa inteira para a memória.
 *
 * A carga é integral e antecipada. É o que faz a reprodução não acessar o
 * cartão nenhuma vez, satisfaz o RNF06 por construção, torna o RNF07 quase
 * gratuito e reduz "reiniciar a faixa" a reinterpretar do byte zero.
 *
 * Os bytes saem como estão no arquivo, BOM e quebras de linha incluídos: o
 * que é ou não faixa válida quem decide é o interpretador. Quando esta função volta,
 * nada da faixa fica aberto nem emprestado — o cartão pode sair do soquete na
 * nota seguinte.
 *
 * @param track Índice devolvido por @ref storage_scan.
 * @param buf   Destino, de tamanho fixo. Convém que seja ::STORAGE_FILE_MAX.
 * @param len   Capacidade de @p buf em bytes. Valores acima de
 *              ::STORAGE_FILE_MAX são tratados como ::STORAGE_FILE_MAX, o
 *              mesmo teto com que a varredura marca a faixa como inválida.
 *
 * @return Quantidade de bytes escritos em @p buf — zero para arquivo vazio —,
 *         ou um erro negativo:
 * @retval -EINVAL @p buf nulo ou @p len igual a zero.
 * @retval -ENOENT @p track fora da biblioteca, ou o arquivo não está mais no
 *                 cartão.
 * @retval -ENODEV Sem cartão (no `storage_host`, pasta ausente).
 * @retval -EFBIG  O arquivo não cabe em @p len. **Nada foi escrito**: o
 *                 arquivo é rejeitado, nunca truncado.
 * @retval -EIO    Falha de leitura. @p buf pode ter sido escrito em parte.
 *
 * @warning **Bloqueia**, pelo mesmo motivo de @ref storage_scan. Chamar da
 *          `loader`, nunca da `player`.
 *
 * @see @ref storage_error_text para a mensagem de cada erro.
 */
int storage_load(int track, uint8_t *buf, size_t len);

/**
 * @brief Mensagem de tela para um erro de @ref storage_scan ou
 *        @ref storage_load.
 *
 * É o texto da tela de erro que a interface mostra por ~2 s antes de voltar
 * à biblioteca. Cabe numa linha da tela — 16 colunas com a UNSCII-8 —, sem
 * acento, como os demais textos de estado.
 *
 * @param err Código negativo devolvido pelo contrato.
 * @return Texto constante. Código fora do contrato devolve uma mensagem
 *         genérica em vez de NULL.
 */
const char *storage_error_text(int err);

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

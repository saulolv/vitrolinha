/**
 * @file last_track.h
 * @brief A última faixa, lembrada entre reinicializações (RF10).
 *
 * **Onde mora.** No subsistema `settings` do Zephyr, com o NVS sobre a
 * partição `storage` da flash interna — a `zephyr,settings-partition` que a
 * própria ZBook declara. Nunca no cartão: o caminho de escrita do
 * `sdhc_spi` tem um laço de espera sem teto, e a regra do projeto é não
 * escrever no cartão.
 *
 * **Como a faixa é reconhecida.** Pelo nome exibido e pelo índice, juntos.
 * O índice sozinho apontaria outra música depois de um arquivo novo no
 * cartão, porque a lista segue a ordem do diretório; o nome sozinho não
 * distingue duas faixas de mesmo nome. Na volta vale a faixa válida com o
 * nome gravado — a do índice gravado, se o nome bater nela, senão a primeira
 * com esse nome. O nome 8.3 do arquivo seria mais estável, mas a biblioteca
 * não o guarda, e guardá-lo custaria 32 x 13 B de RAM para sempre.
 *
 * **Quando grava.** Numa thread própria, de prioridade mínima, que olha o
 * instantâneo a cada ::LAST_TRACK_POLL_MS e grava quando a faixa está
 * parada há ::LAST_TRACK_DEBOUNCE_MS — trocas rápidas pelo encoder não
 * gastam flash — e **só com o player fora de ::PLAYER_PLAYING**. No RP2350 o
 * driver de flash trava as interrupções durante a escrita e o apagamento
 * (o XIP sai do ar), e um apagamento de setor do NVS passa de dezenas de
 * milissegundos: no meio da música, atrasaria a próxima nota. A
 * consequência aceita é que a faixa que toca sem pausa só é lembrada quando
 * parar ou pausar; quem desliga a placa no meio dela volta à anterior.
 *
 * **Quando restaura.** Uma vez por boot, na primeira biblioteca pronta: o
 * `main` chama @ref last_track_restore depois de pôr o player de pé e a cada
 * troca de cartão, e entrega o índice ao player (::CMD_CUE) e à tela
 * (`display_cue`). Nada toca sozinho.
 */

#ifndef VITROLINHA_LAST_TRACK_H_
#define VITROLINHA_LAST_TRACK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Chave no `settings`. */
#define LAST_TRACK_KEY "vitrolinha/last"

/** @brief Período da thread que observa o instantâneo. */
#define LAST_TRACK_POLL_MS 500

/** @brief Quanto a faixa tem de ficar a mesma antes de ir para a flash. */
#define LAST_TRACK_DEBOUNCE_MS 2000

/**
 * @brief Inicializa o `settings` e lê a faixa gravada, se houver.
 *
 * Não inicia a thread de gravação: ver @ref last_track_start.
 *
 * @return 0, ou o erro de `settings_subsys_init`/`settings_load_subtree`.
 */
int last_track_init(void);

/** @brief Põe de pé a thread que grava a faixa. Chamar uma vez. */
void last_track_start(void);

/**
 * @brief Procura a faixa gravada na biblioteca. Vale uma vez por boot.
 *
 * @return Índice da faixa, ou:
 * @retval -EAGAIN    @ref last_track_init ainda não leu a flash, ou a
 *                    biblioteca não está pronta ou está vazia: tente de
 *                    novo na próxima varredura.
 * @retval -ENOENT    Nada gravado, ou a faixa não está neste cartão.
 * @retval -EALREADY  Já respondeu neste boot.
 *
 * @note Segura a trava da lista pelo tempo de uma cópia. Não chamar do
 *       `player`.
 */
int last_track_restore(void);

/**
 * @brief Uma passada da thread de gravação.
 *
 * @param now_ms Instante corrente, em ms de `k_uptime_get`.
 *
 * @retval 0       A faixa corrente já está gravada, ou acabou de ser.
 * @retval -EAGAIN Sem faixa, ou faixa nova, ainda no debounce.
 * @retval -EBUSY  Tocando: gravar agora atrasaria a próxima nota.
 * @retval -ENOENT A faixa não está na biblioteca publicada.
 * @retval outro   Erro de `settings_save_one`; tenta de novo na próxima.
 */
int last_track_poll(int64_t now_ms);

/**
 * @brief Esquece o que está em RAM, como num boot sem nada gravado.
 *
 * Não apaga a flash. Existe para os testes simularem a reinicialização:
 * esquecer, chamar @ref last_track_init de novo e ver a faixa voltar da
 * flash.
 */
void last_track_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_LAST_TRACK_H_ */

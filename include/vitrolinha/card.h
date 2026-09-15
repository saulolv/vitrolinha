/**
 * @file card.h
 * @brief O cartão: montagem, presença e os três estados que dela decorrem.
 *
 * É a metade de plataforma do módulo `storage` — a que sabe que existe FAT,
 * SPI e pino de detecção. A outra metade, @ref storage.h, fala de faixas e
 * não sabe nada disto. A separação é o que permite à varredura (issue #10) e
 * à carga (issue #11) tratarem "sem cartão" como um código de erro em vez de
 * uma consulta a hardware.
 *
 * O vocabulário vem do CONTEXT.md: o cartão tem **três** estados que pedem
 * tratamento distinto, e não dois. "Ausente" e "presente mas ilegível" pedem
 * mensagens diferentes na tela, e a tentativa de remontagem só faz sentido
 * num deles.
 *
 * @note Nada aqui é chamável da thread `player` nem do callback do
 *       temporizador. @ref card_refresh chega a montar o cartão, e uma
 *       montagem pode custar mais de um segundo — ver a nota de bloqueio
 *       naquela função.
 *
 * @see docs/adr/0007-monitor-do-cartao-em-thread-propria.md
 */

#ifndef VITROLINHA_CARD_H_
#define VITROLINHA_CARD_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ponto de montagem do cartão.
 *
 * O `disk-name` do nó `sdhc0` da ZBook é `"SD"`, e o subsistema de arquivos
 * exige a barra inicial. No alvo de simulação o mesmo nome chega por
 * `CONFIG_FS_FATFS_CUSTOM_MOUNT_POINTS`, para que o caminho seja idêntico nos
 * dois alvos.
 */
#define CARD_MOUNT_POINT "/SD"

/**
 * @brief Os três estados do cartão.
 *
 * A ordem não é arbitrária: ::CARD_ABSENT é zero para que o estado inicial,
 * antes de qualquer varredura, seja o conservador.
 */
enum card_state {
	/** Soquete vazio. */
	CARD_ABSENT = 0,
	/** Presente e montado: dá para varrer e ler. */
	CARD_READY,
	/** Presente, mas não montou. Nem some da tela, nem serve. */
	CARD_UNREADABLE,
};

/**
 * @brief Notificação de mudança de estado.
 *
 * Chamada **na thread do monitor**, só quando o estado muda de fato, e já com
 * a trava interna do módulo solta — então pode chamar qualquer coisa deste
 * cabeçalho sem risco de impasse. Não pode bloquear nem demorar: enquanto ela
 * roda, o monitor não observa o cartão.
 *
 * @param state O estado novo.
 */
typedef void (*card_observer_t)(enum card_state state);

/**
 * @brief Registra quem quer saber das mudanças de estado.
 *
 * Um observador só, de propósito: quem precisa saber é a máquina de navegação
 * da interface, e um segundo interessado seria sinal de que o estado deveria
 * estar sendo lido por @ref card_get_state, não empurrado.
 *
 * Registrar **antes** de @ref card_init para receber também a transição
 * inicial — a de quem liga a placa com o cartão já no soquete.
 *
 * @param observer Função a chamar, ou NULL para desligar a notificação.
 */
void card_observe(card_observer_t observer);

/**
 * @brief Prepara o cartão e põe o monitor de presença de pé.
 *
 * Faz uma avaliação síncrona antes de voltar, de modo que
 * @ref card_get_state já responda a verdade quando esta função retornar, e
 * só então cria a thread que observa inserção e remoção.
 *
 * @retval 0         Monitor de pé. O estado inicial está em
 *                   @ref card_get_state — inclusive ::CARD_ABSENT, que não é
 *                   erro de inicialização.
 * @retval -EALREADY Já inicializado. Chamar duas vezes não cria um segundo
 *                   monitor.
 */
int card_init(void);

/**
 * @brief Estado corrente, sem bloquear e sem tocar no cartão.
 *
 * Devolve o que a última avaliação concluiu. É esta a função que a interface
 * chama a cada quadro; a que custa caro é @ref card_refresh.
 *
 * @return O estado corrente.
 */
enum card_state card_get_state(void);

/**
 * @brief Atalho para "dá para varrer e ler agora".
 *
 * @return Verdadeiro se o estado corrente é ::CARD_READY.
 *
 * @note Falso durante a reprodução não interrompe a faixa: ela já está
 *       inteira em RAM (RNF07).
 */
bool card_ready(void);

/**
 * @brief Reavalia o cartão agora, montando ou desmontando se preciso.
 *
 * O monitor chama isto sozinho; a aplicação só precisa chamar para forçar uma
 * tentativa fora da cadência do monitor.
 *
 * Transições possíveis:
 *
 * | De | Cartão | Para | O que acontece |
 * |---|---|---|---|
 * | qualquer | fora | ::CARD_ABSENT | desmonta, se estava montado |
 * | ::CARD_ABSENT | dentro | ::CARD_READY ou ::CARD_UNREADABLE | tenta montar |
 * | ::CARD_UNREADABLE | dentro | ::CARD_UNREADABLE | **não** tenta de novo |
 * | ::CARD_READY | dentro | ::CARD_READY | nada |
 *
 * A linha que importa é a terceira: um cartão que não montou não passa a
 * montar sozinho, e insistir a cada varredura só gastaria o barramento. A
 * nova tentativa vem da remoção e reinserção — ou de uma chamada explícita
 * daqui, depois de o estado ter voltado a ::CARD_ABSENT.
 *
 * @return O estado após a avaliação.
 *
 * @warning **Pode bloquear.** O caminho barato — cartão parado no estado em
 *          que já estava — é uma leitura de pino. Já a montagem inclui a
 *          inicialização do cartão, que o `CONFIG_SD_INIT_TIMEOUT` limita a
 *          1500 ms, e a desmontagem espera o cartão desocupar, limitada pelo
 *          `CONFIG_SD_DATA_TIMEOUT`. É por isso que o monitor tem thread
 *          própria e prioridade baixa.
 */
enum card_state card_refresh(void);

/**
 * @brief Nome do estado, para log e para tela.
 *
 * @param state Estado a nomear.
 * @return Texto constante, sem acento. Valor fora da enumeração devolve
 *         `"desconhecido"` em vez de indexar fora da tabela.
 */
const char *card_state_name(enum card_state state);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_CARD_H_ */

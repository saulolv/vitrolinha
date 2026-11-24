/**
 * @file library.h
 * @brief A biblioteca como o resto do sistema a vê, e a matriz de falhas do
 *        cartão.
 *
 * O @ref storage.h responde "o que há no cartão agora", com um código de
 * erro quando não dá para responder. Isso não basta para a tela nem para o
 * transporte: as três falhas da seção 9 da especificação pedem
 * comportamentos distintos, e o mesmo `-ENODEV` significa "sem cartão" no
 * boot e "cartão removido" no meio da música. Este módulo guarda a memória
 * que falta para distinguir uma coisa da outra, e é o único lugar que decide
 * o que cada falha faz.
 *
 * | Falha | Estado | O que muda |
 * |---|---|---|
 * | Sem cartão no boot | ::LIBRARY_NO_CARD | lista vazia; na inserção, @ref library_refresh monta e varre |
 * | Cartão removido | ::LIBRARY_REMOVED | lista vazia e seguinte/anterior desligados; a faixa em curso, já em RAM, segue |
 * | Arquivo inválido | sem mudança de estado | a faixa ganha `!` por @ref library_load ou @ref library_reject |
 *
 * Os dois estados restantes cobrem o cartão presente que não lê. Eles se
 * separam pela história, não pelo cartão: ::LIBRARY_UNREADABLE é o primeiro
 * cartão que não leu, e ::LIBRARY_REBOOT é o que não leu depois de um outro
 * já ter lido. O segundo caso é a remontagem a quente da issue zephyr#94033,
 * fechada como "not planned": a aplicação tenta, e quando falha a única
 * saída honesta é pedir que a placa seja reiniciada.
 *
 * **Quem chama o quê**:
 *
 * - O `player` só chama @ref library_neighbor e @ref library_state, que não
 *   bloqueiam nem tocam em trava.
 * - A `ui` chama @ref library_copy, que segura uma trava pelo tempo de
 *   copiar a lista, nunca pelo de uma varredura.
 * - @ref library_refresh roda na thread do monitor do cartão, chamada pelo
 *   observador — ver a ADR 0010.
 * - @ref library_load e @ref library_reject são da `loader`.
 *
 * @see docs/especificacao-vitrolinha.md, seção 9
 * @see docs/adr/0010-a-biblioteca-e-dona-da-matriz-de-falhas.md
 */

#ifndef VITROLINHA_LIBRARY_H_
#define VITROLINHA_LIBRARY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <vitrolinha/track.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Em que pé está a biblioteca.
 *
 * Só ::LIBRARY_READY tem faixas. Em todos os outros a lista está vazia e
 * cada estado tem a sua mensagem em @ref library_state_text.
 *
 * ::LIBRARY_NO_CARD é zero para que o estado de quem acabou de ligar a placa,
 * antes de qualquer varredura, seja o conservador.
 */
enum library_state {
	/** Nenhum cartão legível desde o boot, e o soquete está vazio. */
	LIBRARY_NO_CARD = 0,
	/** Cartão lido: a lista é a do soquete. */
	LIBRARY_READY,
	/** Havia biblioteca, e o cartão saiu do soquete. */
	LIBRARY_REMOVED,
	/** Cartão presente que não leu, sem que nenhum outro tenha lido antes. */
	LIBRARY_UNREADABLE,
	/**
	 * Cartão presente que não leu depois de um outro já ter lido: a
	 * remontagem a quente falhou (zephyr#94033).
	 */
	LIBRARY_REBOOT,
};

/**
 * @brief Varre o cartão de novo e publica o resultado.
 *
 * É a reação a toda mudança de estado do cartão: na inserção, monta a lista;
 * na remoção, a esvazia sem tocar no barramento, porque a varredura falha já
 * no empréstimo do volume.
 *
 * A varredura acontece numa cópia de trabalho, e só a publicação segura a
 * trava da lista: a `ui` nunca espera um cartão lento.
 *
 * Toda chamada incrementa @ref library_generation, mesmo quando o estado não
 * muda — a lista pode ter mudado com ele parado, como numa troca de cartão
 * entre duas sondagens.
 *
 * @return Quantidade de faixas, ou o erro de @ref storage_scan que levou ao
 *         estado novo.
 *
 * @warning **Bloqueia**, pelo tempo de uma varredura. Chamar da thread do
 *          monitor do cartão ou da inicialização, nunca do `player`.
 */
int library_refresh(void);

/**
 * @brief Estado corrente, sem bloquear.
 *
 * @return O estado publicado pela última varredura ou carga.
 */
enum library_state library_state(void);

/**
 * @brief Contador de publicações.
 *
 * Muda a cada @ref library_refresh e a cada falha de cartão vista por
 * @ref library_load. É por ele que a `ui` sabe que tem de reconstruir a
 * lista, e que @ref library_reject sabe que o índice que recebeu ainda é da
 * mesma biblioteca.
 *
 * @return Valor corrente. Só a igualdade tem significado.
 */
uint32_t library_generation(void);

/**
 * @brief Copia a lista publicada.
 *
 * @param out     Destino, com pelo menos @p max posições.
 * @param max     Capacidade de @p out.
 * @param skipped Se não nulo, recebe quantos arquivos ficaram de fora por
 *                passarem de ::LIBRARY_MAX — o número do aviso de biblioteca
 *                cheia.
 *
 * @return Quantidade de faixas copiadas, zero fora de ::LIBRARY_READY, ou:
 * @retval -EINVAL @p out nulo ou @p max igual a zero.
 *
 * @note Segura a trava da lista pelo tempo de copiar até 32 entradas. Não
 *       chamar do `player`.
 */
int library_copy(struct track_meta *out, size_t max, size_t *skipped);

/**
 * @brief A faixa seguinte ou a anterior, para o transporte.
 *
 * Pula as faixas inválidas e dá a volta nas pontas. Sem faixa de partida,
 * "seguinte" é a primeira válida e "anterior" a última.
 *
 * É aqui que "seguinte/anterior desabilitados" acontece: fora de
 * ::LIBRARY_READY, ou sem nenhuma faixa válida, a resposta é ::TRACK_NONE, e
 * o `player` simplesmente não troca de faixa.
 *
 * Não bloqueia e não toca em trava: chamável da thread `player`.
 *
 * @param track   Faixa de partida, ou ::TRACK_NONE.
 * @param forward Verdadeiro para a seguinte, falso para a anterior.
 *
 * @return Índice da faixa, ou ::TRACK_NONE. Com uma única faixa válida, a
 *         resposta é ela mesma — que é recomeçar.
 */
int library_neighbor(int track, bool forward);

/**
 * @brief Carrega uma faixa e classifica a falha, se houver.
 *
 * A classificação é a razão de esta função existir em vez de a `loader`
 * chamar @ref storage_load direto: um erro de carga é culpa **do arquivo** ou
 * **do cartão**, e cada um pede uma reação diferente.
 *
 * | Erro | Culpa | Reação |
 * |---|---|---|
 * | `-EFBIG`, `-ENOENT` | do arquivo | a faixa ganha `!`; a biblioteca segue |
 * | `-ENODEV` | do cartão | a biblioteca passa a ::LIBRARY_REMOVED |
 * | `-EIO` | do cartão | a biblioteca passa a ::LIBRARY_REBOOT |
 *
 * Fora de ::LIBRARY_READY, a função responde sem tocar no cartão.
 *
 * @param track      Índice na lista publicada.
 * @param buf        Destino. Tem de comportar ::STORAGE_FILE_MAX bytes: com
 *                   menos, um `-EFBIG` seria culpa do buffer e não do
 *                   arquivo, e a faixa ganharia um `!` injusto.
 * @param len        Capacidade de @p buf.
 * @param generation Se não nulo, recebe a @ref library_generation da lista à
 *                   qual @p track pertence, para entregar a
 *                   @ref library_reject se o interpretador recusar a faixa.
 *
 * @return Bytes carregados, ou erro negativo:
 * @retval -EINVAL @p buf nulo ou @p len menor que ::STORAGE_FILE_MAX.
 * @retval -ENOENT @p track fora da lista, ou o arquivo sumiu do cartão.
 * @retval -EFBIG  O arquivo passa de ::STORAGE_FILE_MAX.
 * @retval -ENODEV Sem cartão.
 * @retval -EIO    Cartão presente que não lê.
 *
 * @warning **Bloqueia**, como @ref storage_load. Chamar da `loader`.
 */
int library_load(int track, uint8_t *buf, size_t len, uint32_t *generation);

/**
 * @brief Marca uma faixa como inválida: o interpretador a recusou.
 *
 * A faixa continua na lista, com `!`, e @ref library_neighbor passa a
 * pulá-la.
 *
 * @param track      Faixa recusada.
 * @param generation O que @ref library_load devolveu junto com ela. Uma
 *                   varredura no meio do caminho troca o significado dos
 *                   índices, e marcar o índice velho na lista nova poria o
 *                   `!` na faixa errada.
 *
 * @retval 0       Marcada.
 * @retval -ESTALE A lista mudou desde a carga. Nada foi marcado.
 * @retval -ENOENT @p track fora da lista.
 */
int library_reject(int track, uint32_t generation);

/**
 * @brief Texto da tela de erro de cada estado.
 *
 * Cabe numa linha da tela, 16 colunas com a UNSCII-8, sem acento.
 *
 * @param state Estado.
 * @return Texto constante. Valor fora da enumeração devolve uma mensagem
 *         genérica em vez de NULL.
 */
const char *library_state_text(enum library_state state);

#ifdef __cplusplus
}
#endif

#endif /* VITROLINHA_LIBRARY_H_ */

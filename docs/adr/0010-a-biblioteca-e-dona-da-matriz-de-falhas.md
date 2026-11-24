# A biblioteca é dona da matriz de falhas, e varre na thread do monitor

A seção 9 da especificação define três falhas com comportamentos distintos:
sem cartão no boot, cartão removido tocando e arquivo inválido. Até a issue
#12, o código tratava as três como uma só: o `storage` devolve `-ENODEV` ou
`-EIO`, e quem chama vê só o código, sem saber se o `-ENODEV` é o boot sem
cartão ou a remoção no meio da música, nem se o `-EIO` de uma carga é culpa
do arquivo ou do cartão.

Decidimos criar o módulo **`library`** (`src/library.c`), que é dono da
lista que o resto do sistema vê e o **único** lugar que decide o que cada
falha faz. Ele guarda a única memória que falta ao `storage` — se algum
cartão já leu desde o boot — e com ela separa cinco estados:

| Estado | Quando | Tela |
|---|---|---|
| `LIBRARY_NO_CARD` | nenhum cartão leu, soquete vazio | "Sem cartao" |
| `LIBRARY_READY` | lista do soquete | — |
| `LIBRARY_REMOVED` | havia lista, o cartão saiu | "Cartao removido" |
| `LIBRARY_UNREADABLE` | presente, não leu, nenhum leu antes | "Cartao ilegivel" |
| `LIBRARY_REBOOT` | presente, não leu, um outro já leu | "Reinicie a placa" |

O último estado é a remontagem a quente da issue zephyr#94033, fechada como
*not planned*. O `card` tenta remontar a cada reinserção, porque custa pouco
e às vezes funciona; quando falha, a biblioteca não tem como distinguir um
cartão ruim de uma pilha SD presa, e "reinicie a placa" é o conselho certo nos
dois casos — depois do boot, um cartão ruim de verdade aparece como "Cartao
ilegivel".

A carga também classifica: `-EFBIG` e `-ENOENT` são culpa do arquivo, e a
faixa ganha `!`; `-ENODEV` e `-EIO` são culpa do cartão, e a lista inteira
cai. "Seguinte/anterior desabilitados" é consequência, não regra à parte: o
`player` pergunta a `library_neighbor`, que responde `TRACK_NONE` sempre que
não há faixa tocável.

## Onde a varredura roda

A biblioteca tem de ser varrida a cada transição do cartão. A varredura
bloqueia — abre cada arquivo para ler o cabeçalho —, então a pergunta é em
que thread.

### Considered Options

- **Na fila de trabalho do sistema.** Rejeitada pelo mesmo motivo da ADR
  0007: a fila é cooperativa (`CONFIG_SYSTEM_WORKQUEUE_PRIORITY=-1`), e uma
  varredura num cartão lento seguraria o `player`.

- **Numa thread própria da biblioteca.** Rejeitada por custo sem benefício:
  outra pilha, só para esperar o observador do `card` acordá-la, quando a
  thread do monitor já existe, já é a de menor prioridade e já é a única que
  bloqueia por natureza.

- **Na futura `loader`.** Rejeitada pelo motivo que a ADR 0007 deu para não
  sondar de lá: o cartão que entra sem ninguém ter pedido faixa precisa ser
  varrido mesmo assim.

- **No observador do `card`, na thread do monitor.** Escolhida.

## Consequences

O contrato do observador em `card.h` mudou de "não pode bloquear" para "pode
bloquear". O custo é o mesmo de um empréstimo do volume (ADR 0009): enquanto
a varredura roda, o monitor não sonda, e uma remoção no meio dela é vista na
sondagem seguinte — com a varredura já falhando por conta própria, porque as
leituras de um cartão fora do soquete devolvem erro.

A pilha do monitor passa a carregar o caminho da varredura, além do da
montagem. Os 2 KB atuais continuam sendo estimativa; o número sai da medição
da issue #13.

Para a `ui` nunca esperar um cartão lento, a varredura escreve numa cópia de
trabalho e só a publicação segura a trava da lista. São 832 B fixos a mais
(RNF06). O `player` não toca em trava nenhuma: a máscara das faixas tocáveis
cabe em 32 bits e é um atômico.

O índice que a `loader` carrega tem de significar a mesma faixa do começo ao
fim — inclusive quando o interpretador recusa o arquivo e a faixa precisa
ganhar o `!`. Uma trava de E/S serializa carga e varredura, e a recusa leva a
geração da lista: recusar com uma geração velha devolve `-ESTALE` em vez de
marcar a faixa errada.

O `main.c` registra o observador **depois** de `card_init` e varre em
seguida, ao contrário do que o `card.h` sugere: na ordem sugerida, o boot com
cartão varreria duas vezes, e a varredura explícita não pode faltar, porque
no simulador o cartão nunca transita e quem responde é a implementação de
mentira.

Os três consumidores passam pela biblioteca, e nenhum deles fala com o
`storage` direto:

- o `loader` carrega por `library_load` e, quando o parser recusa o
  arquivo, marca a faixa com `library_reject`, levando a geração lida na
  carga;
- o `player` escolhe seguinte, anterior e a primeira faixa por
  `library_neighbor`, e só aceita seleção em `LIBRARY_READY`, ambos lidos
  sem trava. Por isso o `player_init` não recebe mais a contagem de faixas:
  um cartão inserido depois do boot vale para ele no comando seguinte;
- a `ui` relê a lista por `library_copy` a cada mudança de
  `library_generation`, conferida no timer de atualização, e fora de
  `LIBRARY_READY` a primeira linha da lista é o `library_state_text`. Com o
  cartão fora, a faixa em curso segue tocando com o nome dela, porque a
  `ui` guarda os nomes da última lista boa.

A faixa em curso terminar com o cartão fora já é verdade por construção,
porque ela está inteira em RAM. O que segue com as trilhas de interface
(#29, #30) é o desenho da tela de erro em si; aqui ela é a primeira linha da
lista.

`tests/library/` cobre a lógica sobre a implementação de mentira;
`tests/library_card/` induz as três falhas de ponta a ponta, com o monitor de
verdade chamando a biblioteca pelo observador, sobre o disco de mentira.

## Errata

A versão original deste registro dizia 896 B para a cópia de trabalho da
varredura. O certo é 832 B: 32 entradas de `struct track_meta`, de 26 B cada
(24 do nome, 1 do índice e 1 do `valid`, sem preenchimento). O número foi
corrigido no texto acima; a decisão não muda.

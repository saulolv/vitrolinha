# O monitor do cartão é uma thread própria

O cartão precisa ser montado quando entra e desmontado quando sai, e a
interface precisa saber disso sem interrogar o sistema de arquivos a cada
quadro. A pergunta é onde esse trabalho mora.

Decidimos dar ao monitor uma **thread própria, de prioridade 12** — a mais
baixa do sistema —, que sonda o soquete a cada 250 ms e publica o estado num
inteiro atômico. O código está em `src/card.c`.

Três fatos do Zephyr fecham a decisão, e nenhum deles é escolha nossa:

- **Não há interrupção de detecção.** O `sdhc_spi` configura o pino de
  card-detect como `GPIO_INPUT` puro (`sdhc_spi_init`), e a pilha SD só expõe
  a presença por consulta, em `sdhc_spi_get_card_present`. Quem quiser saber,
  pergunta.
- **Montar bloqueia por até 1,5 s.** É o `CONFIG_SD_INIT_TIMEOUT`, que fica no
  padrão de propósito: é o prazo que a especificação física do SD dá ao cartão
  para concluir a inicialização, e encurtá-lo trocaria uma espera rara por
  rejeitar cartões legítimos.
- **A fila de trabalho do sistema é cooperativa.** O
  `CONFIG_SYSTEM_WORKQUEUE_PRIORITY` vale **-1** nesta compilação, o que a põe
  acima de toda thread preemptível.

## Considered Options

- **Interrupção no nó `sd_cd`.** A aplicação registraria um callback de GPIO
  no mesmo pino que o `cd-gpios` entrega à pilha SD. Rejeitada por duas
  razões. A primeira é que resolve o problema errado: saber que o pino mudou é
  a parte barata, e montar continuaria precisando de uma thread que possa
  bloquear por um segundo e meio. A segunda é que poria dois donos no mesmo
  pino, com a aplicação configurando interrupção numa linha que o driver
  considera sua — e conflito de pino é o tipo de coisa que funciona na bancada
  e falha na demonstração.

- **`k_work_delayable` na fila de trabalho do sistema.** Não custaria pilha
  nova e é o reflexo natural para trabalho periódico. Rejeitada pelo terceiro
  fato acima: com a fila em prioridade cooperativa, uma montagem de 1,5 s
  seguraria **todas** as threads preemptíveis do sistema, o `player` entre
  elas. O disparo da nota sobreviveria, porque acontece em contexto de
  interrupção, mas a máquina de estados do `player` não rodaria — e o trabalho
  inteiro é sobre isso não acontecer.

- **Sondar de dentro da futura thread `loader`.** Economizaria uma thread, já
  que a `loader` também é de prioridade baixa e também bloqueia em E/S de
  cartão. Rejeitada por acoplamento: amarraria "reparar que o soquete esvaziou"
  a "alguém pediu uma faixa", e o caso que mais importa — o cartão que nunca
  entrou — é justamente aquele em que ninguém pede faixa nenhuma. Pode ser
  reavaliada na E5, quando a `loader` existir.

- **Thread própria, prioridade 12, sondagem de 250 ms.** Escolhida.

## Consequences

O custo é uma thread e a pilha dela, hoje em 2 KB. O número definitivo sai da
medição do `CONFIG_THREAD_ANALYZER` na E6 (issue #13), que é critério de
avaliação do trabalho — não de estimativa nossa.

A latência de detecção passa a ser de até 250 ms. É irrelevante contra o gesto
humano de encaixar um cartão, e o intervalo não precisa ser menor: a sondagem
em si é uma leitura de pino, mas encurtá-la não encurtaria a montagem, que é
onde o tempo está.

Em troca, três propriedades que o resto do sistema pode usar sem pensar:

- **Ninguém mais bloqueia por causa do cartão.** `card_get_state()` é a leitura
  de um inteiro atômico. A interface pode chamá-la a cada quadro.
- **O `player` não é preterido em momento nenhum.** A thread que bloqueia é a
  de menor prioridade do sistema, abaixo até da fila de trabalho do LVGL.
- **Os três estados do CONTEXT.md existem de verdade** — ausente, presente e
  legível, presente e ilegível —, em vez de um booleano que obrigaria a tela de
  erro a adivinhar a mensagem.

### O que a sondagem não resolve

Um cartão que sai no meio de uma leitura ainda faz o `fs_read` corrente
falhar; o monitor só perceberá até 250 ms depois. Isso não ameaça a
reprodução, porque a faixa já está inteira em RAM (RNF07), e é a matriz de
falhas da issue #12 que decide o que a tela mostra em cada caso.

### A tentativa de montagem é uma por inserção

Um volume não se conserta sozinho dentro do soquete. Depois de uma montagem
falhada, o módulo fica em "presente e ilegível" e **não** insiste a cada
sondagem — a nova tentativa vem da remoção e reinserção. Insistir gastaria
barramento para sempre em troca de nada, e é justamente o que a issue
[zephyr#94033](https://github.com/zephyrproject-rtos/zephyr/issues/94033),
fechada como *not planned*, mostra não funcionar.

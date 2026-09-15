# Faixas vizinhas não são pré-carregadas

O RNF03 dá 100 ms entre o botão e o efeito audível, **incluindo seguinte e
anterior**, que exigem abrir e interpretar outro arquivo. A saída de projeto
prevista para o caso de o custo estourar o orçamento era pré-carregar as faixas
vizinhas em buffers extras: mais 12 KB de RAM, ainda de tamanho fixo, mantendo o
RNF06.

Decidimos **não pré-carregar**. A medição está em
[docs/medicoes/rnf03-abertura-de-faixa.md](../medicoes/rnf03-abertura-de-faixa.md)
e a bancada em `bench/rnf03/`.

## Considered Options

- **Pré-carregar as faixas vizinhas**, anterior e seguinte, em dois buffers de
  4 KB além do buffer corrente. Rejeitado por não ser necessário — e a hora de
  rejeitar é agora, não depois: a pré-carga contamina a máquina de estados do
  `player` com três buffers e uma política de invalidação (o que acontece com o
  buffer vizinho quando o encoder salta sete posições na lista?), e nada disso
  se justifica sem um número que o exija.

- **Não decidir ainda e medir na placa.** Rejeitado porque manteve a issue #8
  aberta bloqueando a E4 e a E5 sem necessidade. O que faltava saber era
  *quantos setores* o `fs_open` custa, e isso é software puro acima do
  `disk_access`: o simulador responde com exatidão, desde que o volume tenha a
  geometria de um cartão de verdade.

- **Não pré-carregar.** Escolhida.

## Consequences

A pior faixa possível — 4096 B, o teto do buffer da aplicação — custa **10
setores em 4 chamadas de leitura**. Com o tempo de acesso de 1 ms que a
especificação física do SD fixa para todo cartão SDHC, isso dá **11,8 ms**,
contra **60 ms** de orçamento do armazenamento (100 ms do requisito, menos 20 ms
de antirrebote do botão e 20 ms de reserva para o interpretador).

O que sustenta a decisão não é a folga de 48 ms, e sim a distância até o ponto
de quebra: **o orçamento só estoura se o cartão levar mais de 5,8 ms por bloco**,
quase seis vezes o TAAC especificado. A decisão sobrevive a um cartão bastante
pior do que o esperado.

Duas consequências de projeto:

- O `storage_load` continua sendo uma função síncrona que devolve a faixa
  inteira num buffer estático. A `loader` chama, espera e pronto.
- O buffer de faixa continua sendo **um**. O consumo de RAM do armazenamento
  fica em 4 KB, e não em 12.

### O que a medição não cobre

Se a resposta R1 do cartão não couber nos nove bytes do pacote de comando, o
`sdhc_spi` cai num laço de `k_msleep(10)` — 10 ms por comando, dois comandos por
chamada de leitura. É comportamento de cartão, não de software, e nenhum modelo
alcança. A mesma bancada compila para a placa e roda com `k_cycle_get_64()` de
verdade; conferir isso é item da E1.

Se aparecer, esta decisão **não** se inverte: a causa estaria na folga de bytes
do pacote de comando do driver, e pré-carga trataria o sintoma pagando 12 KB. A
decisão de reabrir a pré-carga exige um número que mostre o custo alto **sem**
essa patologia.

### Sobre medir no simulador

O `native_sim` trata a execução de código como instantânea: cronometrar trabalho
de processador ali devolve zero. A bancada demonstra isso na primeira seção do
relatório, de propósito, e por isso separa o que mede do que modela — a
contagem de setores é medida e vale igual na placa; o tempo é modelo, com os
bytes deduzidos do código do Zephyr e uma varredura de sensibilidade sobre o
único termo que não é dedutível. Isso não substitui a bancada da E1, pela mesma
razão registrada na [ADR 0005](0005-simulacao-em-native-sim.md): tempo simulado
não é evidência de tempo real.

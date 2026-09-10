# `base_us` tem duas leituras, e só uma função pode interpretá-lo

O contrato `struct player_snapshot` foi congelado antes de as trilhas se
dividirem, com quatro campos: estado, `base_us`, `total_us` e faixa. Com
apenas esses campos não dá para expressar as duas coisas de que a tela
precisa: "tocando desde tal instante" e "congelado em tal posição". Se
`base_us` fosse sempre um instante de `k_uptime`, o relógio da tela
continuaria correndo com a música pausada.

Decidimos dar ao campo duas leituras, conforme o estado: em `PLAYER_PLAYING`
é o instante em que a faixa estava na posição zero; em `PLAYER_PAUSED` e
`PLAYER_STOPPED` é o próprio decorrido, congelado. Em troca da sobrecarga,
**nenhum chamador lê o campo diretamente**: `snapshot_elapsed_us()` é o único
lugar do sistema que sabe da distinção, e é testado exaustivamente.

## Considered Options

- **Acrescentar um campo `elapsed_us`.** Mais honesto, e foi a primeira
  escolha. Rejeitado porque o contrato já estava congelado nas issues #1 a #4
  e é pré-requisito das três trilhas: mudá-lo agora custa uma renegociação
  para economizar uma função de oito linhas.
- **Deixar a interface congelar o valor ao ver a transição para pausado.**
  Rejeitado: poria estado do player dentro da `ui`, que é exatamente a
  fronteira que o projeto está defendendo.

## Consequences

Cai bem no fim de faixa: o player para com `base_us == total_us`, e a barra
fica em 100% sem nenhum caso especial. Também é o que torna barato o RF09
(avanço automático) — de "parar" para "carregar a próxima" é uma linha.

Se um dia o contrato for reaberto, a troca por um campo próprio é mecânica:
muda `snapshot_elapsed_us` e mais nada.

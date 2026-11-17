# Quem lê o cartão pega o volume emprestado

A varredura da biblioteca (issue #10) e a carga da faixa (issue #11) leem o
volume numa thread; o monitor do cartão (ADR 0007) o desmonta noutra, quando o
soquete esvazia. Nada no caminho entre as duas as separa:

- **O FatFs roda sem reentrância.** O `FF_FS_REENTRANT` fica desligado — ver
  o `CLAUDE.md`, na parte de `CONFIG_FS_FATFS_LFN` —, então o próprio FatFs
  não trava o objeto de trabalho do volume.
- **O `fs_read` do Zephyr não trava nada.** O `fs_unmount` segura a trava da
  lista de montagens (`subsys/fs/fs.c`), mas a leitura não a consulta.
- **O `fatfs_unmount` zera o volume.** Ele chama `f_mount(NULL, ...)`, que
  desliga o objeto `FATFS` que uma leitura em curso ainda está usando.

O monitor tem a menor prioridade do sistema, mas isso não o impede de rodar no
meio de uma leitura: o `sdhc_spi` dorme esperando o cartão (`k_msleep(10)` no
laço da resposta R1), e é justamente aí que a thread de prioridade 12 ganha a
CPU. Um cartão puxado no meio da varredura produziria uma leitura sobre um
volume desmontado por baixo dela.

Decidimos que **quem lê pega o volume emprestado**: `card_acquire()` segura a
trava do módulo `card` e devolve 0 só com o cartão em `CARD_READY`;
`card_release()` a solta. Enquanto durar o empréstimo, o `card_refresh` do
monitor espera.

## Considered Options

- **Não travar e documentar a corrida.** Custa zero linhas. Rejeitada porque
  a janela não é teórica — ela se abre exatamente no gesto que a issue #12
  manda induzir na bancada, tirar o cartão durante o uso —, e o resultado não
  seria um erro de leitura, e sim estado corrompido dentro do FatFs.

- **Ligar `FF_FS_REENTRANT`.** É a solução que o FatFs oferece. Rejeitada
  pelo que ela arrasta: exige `LFN_MODE_BSS` desligado, e com ela o
  `FF_FS_TIMEOUT` do Zephyr vira `K_FOREVER` — uma leitura presa num cartão
  morto seguraria a trava para sempre. E ela resolveria o problema errado: a
  trava do FatFs protege cada chamada, mas a varredura é uma sequência de
  chamadas (`opendir`, `readdir`, `open`, `read`) que não pode ter uma
  desmontagem no meio.

- **O `storage` ser dono da montagem.** Uma thread só tocaria no sistema de
  arquivos, e a corrida deixaria de existir. Rejeitada porque desfaz a ADR
  0007: amarraria "reparar que o soquete esvaziou" a "alguém pediu uma
  faixa", e o cartão que nunca entrou é justamente o caso em que ninguém pede.

- **Trava própria no `storage`.** Rejeitada por não resolver nada: quem
  desmonta é o `card`, e o `card` não a conheceria.

- **Empréstimo pela trava do `card`.** Escolhida.

## Consequences

O código de erro do contrato do `storage` sai do empréstimo de graça:
`card_acquire()` já devolve `-ENODEV` para soquete vazio e `-EIO` para cartão
ilegível, que são os dois códigos de `storage_scan` e `storage_load`.

A varredura e a carga ficam serializadas uma com a outra sem trava extra,
porque as duas pegam o mesmo volume emprestado. A tabela interna do `storage`
que liga índice a nome de arquivo só é tocada com o empréstimo em mãos.

Os custos:

- **A remoção é vista mais tarde.** Durante o empréstimo, o monitor não
  avalia o cartão. Uma varredura de 32 faixas dura dezenas de milissegundos;
  a remoção aparece na primeira sondagem depois dela, e não 250 ms após o
  gesto. Irrelevante para quem olha a tela.
- **Quem pede emprestado pode esperar uma montagem.** Se o monitor estiver
  montando, `card_acquire()` bloqueia até 1,5 s (`CONFIG_SD_INIT_TIMEOUT`). É
  por isso que a função não é chamável do `player`, e por isso varredura e
  carga moram em threads de prioridade baixa.
- **A trava é recursiva.** Quem tem o volume emprestado não pode chamar
  `card_refresh()`: a avaliação rodaria na mesma thread, passaria pela trava e
  desmontaria o volume do próprio chamador. Está escrito no contrato.

`tests/card/` comprova o essencial com o disco de mentira: com o volume
emprestado, o cartão sai do soquete e o estado continua `CARD_READY` por
quatro sondagens; devolvido o empréstimo, a remoção aparece.

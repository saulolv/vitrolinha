# A aplicação nunca formata o cartão

O `CONFIG_FS_FATFS_MOUNT_MKFS` do Zephyr vale **`y` por padrão**, e o padrão
estava valendo neste projeto: o `FAT_FILESYSTEM_ELM` vem ligado do defconfig da
ZBook, e essa opção não depende de mais nada. O efeito está escrito no próprio
texto de ajuda da opção:

> Note: This option is destructive to data and will automatically destroy your
> disk, if mount attempt fails.

Ou seja: um cartão cuja partição estivesse levemente danificada entraria na
placa com a biblioteca do usuário e sairia **vazio e formatado**, sem erro,
sem aviso e sem pergunta.

Decidimos **desligar a opção** e, além dela, passar `FS_MOUNT_FLAG_NO_FORMAT`
na montagem.

## Considered Options

- **Deixar o padrão.** Tem um argumento a favor, e não é ruim: um cartão novo
  e não formatado passaria a funcionar sozinho, sem o usuário precisar formatá-lo
  no computador. Rejeitada porque o preço é pago por outra pessoa — quem tem
  música no cartão — e porque a falha que dispara a formatação não distingue
  "cartão em branco" de "cartão com sistema de arquivos danificado". Trocar
  silenciosamente a biblioteca de alguém por um volume vazio é a pior coisa que
  este firmware poderia fazer, e ele faria isso por omissão.

- **Desligar só o `FS_MOUNT_FLAG_NO_FORMAT`, mantendo o Kconfig.** Rejeitada
  por ser a metade mais fraca: a sinalização protege esta montagem, mas o
  código de formatação continua no binário, pronto para a próxima montagem que
  alguém escrever.

- **Desligar só o Kconfig.** Basta, e é o que de fato tira o `f_mkfs` do
  binário — com `FS_FATFS_MOUNT_MKFS=n` e sem `FILE_SYSTEM_MKFS`, o
  `FS_FATFS_MKFS` também sai. Rejeitada como solução única porque depende de uma
  linha de `prj.conf` que ninguém relê: basta um módulo futuro selecionar
  `FILE_SYSTEM_MKFS` para o padrão voltar a valer.

- **Desligar o Kconfig e sinalizar a montagem.** Escolhida.

## Consequences

O cartão precisa chegar formatado em FAT. É o que já se espera de um cartão com
música dentro, e é o que o usuário faz no computador ao copiar os arquivos.

Um cartão sem sistema de arquivos reconhecível passa a ser "presente e
ilegível" — um dos três estados do CONTEXT.md — em vez de virar um cartão vazio
e montado. A mensagem na tela é da issue #12.

O `f_mkfs` sai do binário: a verificação em `build/zephyr/.config` mostra
`CONFIG_FS_FATFS_MKFS` desligado no firmware. Formatar deixa de ser algo que o
firmware escolhe não fazer e passa a ser algo que ele não sabe fazer.

As bancadas e os testes continuam formatando, porque precisam criar o volume
que vão medir — `bench/rnf03/` e `tests/card/` ligam `FILE_SYSTEM_MKFS`
explicitamente, nas suas próprias configurações. Não é incoerência: nenhum dos
dois roda na placa do usuário, e em `tests/card/` a formatação automática fica
**desligada** de propósito, justamente para que um volume ilegível continue
ilegível e o estado "presente e ilegível" possa ser testado.

# Vitrolinha

Tocador de músicas embarcado: lê melodias de um cartão microSD, mostra-as num
menu no OLED e as reproduz pelo buzzer. Este arquivo fixa o vocabulário do
projeto — o mesmo termo tem de significar a mesma coisa no código, nas issues,
nos commits e no relatório.

Só entram aqui termos próprios deste domínio. Conceito geral de programação,
ainda que o projeto use muito, não entra.

## Biblioteca e faixas

**Faixa**:
Uma melodia da biblioteca, identificada por um índice de 0 a 31.
_Avoid_: música, arquivo, som, som escolhido

**Biblioteca**:
O conjunto das faixas disponíveis no cartão, com no máximo 32 entradas.
_Avoid_: playlist, lista de reprodução, catálogo

**Nome da faixa**:
O texto exibido na tela, tirado do cabeçalho RTTTL do arquivo. Não é o nome
do arquivo no sistema de arquivos.
_Avoid_: título, nome do arquivo

**Faixa inválida**:
Faixa que existe na biblioteca mas que o interpretador rejeitou. Continua
listada, marcada com `!`.
_Avoid_: faixa corrompida, faixa quebrada, erro de faixa

**Cartão**:
O microSD. Tem três estados que pedem tratamento distinto: ausente, presente
e legível, presente e ilegível.
_Avoid_: SD, disco, armazenamento, storage

## Reprodução

**Evento**:
A unidade que atravessa a fila até o disparo: um instante e uma frequência.
Frequência zero é silêncio. Cada nota produz dois eventos — o tom e o
silêncio da articulação.
_Avoid_: nota (uma nota não é um evento), amostra, comando de som

**Nota**:
Uma altura e uma duração no arquivo RTTTL, antes de virar evento.
_Avoid_: tom, som, frequência

**Fatia**:
A duração completa que uma nota ocupa na grade, incluindo o silêncio da
articulação no fim.
_Avoid_: slot, intervalo, janela

**Articulação**:
O silêncio no fim de cada fatia, que separa notas consecutivas de mesma
altura. Fica dentro da fatia, para não deslocar a grade.
_Avoid_: gap, pausa (pausa é outra coisa), staccato

**Pausa**:
Silêncio escrito no arquivo, com `p` no RTTTL: uma fatia inteira sem som.
_Avoid_: silêncio, articulação

**Grade**:
A régua temporal da faixa, contada em subdivisões inteiras de 1/64 desde o
início. É o que impede o erro de arredondamento de se acumular nota a nota.
_Avoid_: timeline, relógio, cronograma

**Base temporal**:
O ponto de referência do qual todo instante da faixa é calculado. Desliza
quando a reprodução é retomada de uma pausa.
_Avoid_: t zero, início, offset base

**Transposição**:
O deslocamento da faixa inteira em oitavas inteiras, até a nota mais grave
passar do limiar de resposta do piezo. Preserva os intervalos.
_Avoid_: afinação, ajuste de tom, correção

**Instantâneo**:
A fotografia do que o player está fazendo, publicada pelo lado do player e
lida sem bloqueio pela interface. É a única exceção à comunicação por filas.
_Avoid_: estado global, snapshot, contexto compartilhado

## Controles

**Transporte**:
O conjunto tocar/pausar, seguinte e anterior. É global e independente da tela
exibida.
_Avoid_: controles, player controls, botões

**Navegação**:
O movimento do foco dentro da biblioteca, feito só pelo encoder. Nunca muda
o que está tocando.
_Avoid_: seleção (selecionar é outra coisa), scroll, rolagem

**Selecionar**:
Clicar o encoder sobre uma faixa, o que a toca imediatamente, substituindo a
que estiver tocando. Não existe fila de reprodução.
_Avoid_: escolher, abrir, enfileirar

**Comando**:
Uma ação de transporte já traduzida, a caminho do player pela fila. Não sabe
qual tela estava visível quando foi produzido.
_Avoid_: evento (evento é da reprodução), mensagem, ação

## Etapas e bancada

**Bring-up**:
A validação de que um periférico responde como o devicetree descreve, antes
de existir funcionalidade em cima dele.
_Avoid_: teste de hardware, POC, validação

**Arquivo de calibração**:
Faixa de bancada com N notas idênticas de duração conhecida, usada para medir
o erro temporal acumulado.
_Avoid_: teste, benchmark, arquivo de referência

**Implementação de mentira**:
Implementação de um contrato que serve dados embutidos no binário, para
destravar uma trilha antes de a de verdade existir e para tornar
reproduzíveis os caminhos de falha.
_Avoid_: mock, stub, dummy, fake

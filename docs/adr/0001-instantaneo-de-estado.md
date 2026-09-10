# Instantâneo de estado: escritor com interrupções travadas, leitor sem bloqueio

A interface precisa do tempo decorrido a cada quadro, e pedi-lo ao `player`
por fila o obrigaria a responder em ritmo de interface — o acoplamento que a
arquitetura existe para evitar. A saída é um registro compartilhado escrito
só pelo lado do player, mas "o lado do player" são dois contextos: a thread
da máquina de estados e o callback do temporizador, que roda em interrupção.
Uma estrutura de 24 bytes escrita por dois contextos e lida por um terceiro
pode ser lida pela metade.

Decidimos usar um contador de versão (`src/seqlock.c`) com as duas metades
deliberadamente assimétricas: **a escrita desabilita interrupções** por
algumas dezenas de ciclos, o que exclui um escritor do outro; **a leitura
nunca bloqueia e nunca desabilita interrupções**, apenas repete a cópia se
detectar que uma escrita a atravessou.

## Considered Options

- **Mutex ou `k_spinlock` nos dois lados.** Rejeitado: o callback do
  temporizador roda em interrupção e não pode esperar em mutex, e um leitor
  que travasse interrupções somaria o tempo da leitura ao desvio de início da
  nota, que o RNF02 limita a 5 ms.
- **`irq_lock` também no leitor.** Correto e mais simples, mas paga o mesmo
  preço acima: seria a interface gráfica atrasando a música.
- **Buffer duplo com índice atômico.** Funciona, mas gasta o dobro de memória
  e não dispensa a exclusão entre os dois escritores, que é metade do
  problema.

## Consequences

Vale para um núcleo só. O Zephyr não tem porte SMP para Cortex-M, então isso
não é limitação prática hoje — mas se algum dia houver, `irq_lock` deixa de
excluir um escritor rodando no outro núcleo, e o mecanismo tem de mudar. O
aviso está no cabeçalho.

O laço de repetição do leitor só é tomado quando uma interrupção preempta a
cópia, o que `native_sim` não consegue reproduzir. A lógica de repetição é
testada diretamente sobre o `seqlock`, e a aresta correspondente em
`snapshot_read` está registrada em `tests/coverage-exclusions.json`.

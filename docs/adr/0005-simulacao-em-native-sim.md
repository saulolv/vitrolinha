# O alvo de simulação é `native_sim`, não Renode nem QEMU

A placa é escassa e estava no caminho crítico de todas as trilhas: sem
hardware em mãos, nem a interface nem o armazenamento avançavam. A pergunta
natural foi emular a ZBook em Renode.

Decidimos **não emular a placa**, e sim compilar a aplicação para um segundo
alvo, `native_sim/native/64`, com os periféricos emulados do próprio Zephyr:
a tela em janela SDL no formato do SSD1306, e as entradas vindas do teclado
pelos drivers `gpio-keys` reais.

## Considered Options

- **Renode.** Rejeitado por dois motivos independentes. Primeiro, **não
  existe modelo de RP2350 nem de RP2040**: no repositório do Renode a busca
  por `RP2040` não retorna nada, e os dez boards com integração Renode na
  árvore do Zephyr são todos RISC-V, mais um Cortex-R8 virtual. Adotá-lo
  significaria escrever o modelo do SoC — slices de PWM com divisor e wrap,
  I²C, SPI, SSD1306, cartão SD, decodificador de quadratura —, o que é
  construir um simulador, não usar um.

  Segundo, e mais importante: **não sustentaria a tese do trabalho**. O RNF01
  e o RNF02 são afirmações sobre temporização real, e o tempo do Renode é
  virtual, não fiel a ciclo em Cortex-M. Jitter medido em simulador e
  apresentado como evidência seria pior do que não medir, porque tem
  aparência de evidência.

- **QEMU com a placa emulada.** Mesmo impedimento: não há máquina RP2350 nem
  RP2040 no QEMU.

- **`mps2/an521` no QEMU**, que é Cortex-M33 de verdade e roda os testes com
  semântica real de interrupção. Continua interessante para cobrir o ramo de
  repetição do @ref seqlock que o `native_sim` não alcança (ver
  [ADR 0001](0001-instantaneo-de-estado.md)), mas não resolve o problema
  desta decisão: não tem tela nem entradas, então não destrava a interface.

## Consequences

O simulador é fiel onde importa para as trilhas destravadas. A configuração
do LVGL é **idêntica** nos dois alvos — fila de trabalho e prioridade
inclusive —, e o driver SDL opera em mono de 1 bit com tiling vertical, que é
o formato que o SSD1306 reporta. A interface desenvolvida ali passa pelo
mesmo caminho de código da placa.

Não é fiel em três pontos, todos declarados:

- **Não há som.** O Zephyr não tem PWM emulado, e o buzzer continua exigindo
  hardware.
- **A rotação do encoder não passa pelo `gpio-qdec`**, que espera quadratura
  A/B limpa. As setas do teclado viram `INPUT_REL_WHEEL` em
  `src/sim_encoder.c`, que é o evento que o driver emitiria.
- **Nada da E1 é substituído**: limiar do piezo, estalo na troca de nota,
  ordem física dos botões e jitter real seguem sendo bancada.

O custo é manter dois alvos. Ele é contido por três coisas: `prj.conf` e
`app.overlay` guardam só o que é comum, o CI compila os dois a cada push, e o
`CMakeLists.txt` recusa qualquer alvo que não seja um dos dois — inclusive o
`native_sim` de 32 bits, porque o nome dos arquivos em `boards/` tem de casar
com a string de build e a variante de 32 bits exigiria SDL em i386.

A issue #6 continua aberta. O critério dela é gravar e piscar na placa, e o
que isso prova é o caminho ferramenta → binário → hardware; simulador não
prova. O que mudou é que ela deixou de bloquear as outras trilhas.

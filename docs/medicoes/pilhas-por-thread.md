# Pilha por thread: quanto cada uma usa e por que tem o tamanho que tem

**Issue:** [#13](https://github.com/saulolv/vitrolinha/issues/13) ·
**Ferramenta:** `CONFIG_THREAD_ANALYZER` mais `scripts/stack_report.py` ·
**Alvo:** `zbook@p2/rp2350b/m33`

> **Situação: instrumentação pronta, medição pendente.** A tabela de
> resultado, no fim deste documento, só pode sair da placa — e a placa ainda
> não rodou este roteiro. O que está aqui é o que dá para fixar sem ela: a
> configuração do analisador, o inventário das pilhas declaradas, a regra de
> folga e o roteiro que produz a tabela.

O consumo de memória por thread é critério de avaliação explícito do
trabalho: não basta dizer quanto cada pilha tem, é preciso dizer **quanto ela
usa e por que a folga é a que é**.

## Por que só na placa

O analisador de threads do Zephyr declara `depends on !ARCH_POSIX`, e não por
descuido. No `native_sim` cada thread do Zephyr roda numa pthread do Linux,
com a pilha do sistema hospedeiro: o vetor de pilha que o Zephyr reserva
nunca é tocado, e qualquer medição ali daria uso zero. Diferente da bancada do
RNF03, aqui não existe metade transferível do simulador para a placa — a
pilha que se mede é a do Cortex-M33, com o ABI dele, o compilador dele e os
drivers dele.

Por isso a configuração do analisador mora em
`boards/zbook_rp2350b_m33_p2.conf`, e não em `prj.conf`.

## O que o analisador mede

O `CONFIG_THREAD_ANALYZER` liga o `CONFIG_INIT_STACKS`, que preenche cada
pilha com `0xAA` quando a thread nasce. A medição procura, a partir do fundo,
a primeira palavra que deixou de ser `0xAA`: tudo dali para cima já foi
usado em algum momento. É uma **marca d'água**:

- **Só sobe.** Um pico de um segundo no boot fica registrado para sempre. Por
  isso o intervalo do relatório periódico não importa para o resultado, só
  para quando ele aparece no console.
- **É o pior caso dos caminhos que rodaram, não o pior caso.** Caminho de
  erro que não aconteceu durante a medição não deixou marca. É o motivo de
  existir folga — e o motivo de o roteiro abaixo provocar as falhas de
  propósito.
- **Não vê o que não é thread.** A pilha de interrupção é medida à parte
  (`CONFIG_THREAD_ANALYZER_ISR_STACK_USAGE`), e é ela que importa para o
  disparo das notas: o callback do `k_timer` roda ali.

O estouro não é silencioso nesta placa. Com `CONFIG_HW_STACK_PROTECTION` o
Cortex-M33 usa o limite de pilha do próprio núcleo (`PSPLIM`,
`CONFIG_BUILTIN_STACK_GUARD`): passar do fim da pilha vira exceção com a
thread culpada identificada, e não corrupção da memória vizinha. A folga
existe para que essa exceção não aconteça, não para que ela seja detectada.

## Configuração, e por que cada linha

Em `boards/zbook_rp2350b_m33_p2.conf`:

| Opção | Valor | Por quê |
|---|---|---|
| `CONFIG_THREAD_ANALYZER` | `y` | O critério de avaliação |
| `CONFIG_THREAD_ANALYZER_AUTO` | `y` | Relatório periódico sem código na aplicação, a cada 60 s (padrão) |
| `CONFIG_THREAD_ANALYZER_AUTO_THREAD_PRIORITY` | `10` | **O padrão é 0**, a maior preemptível: a varredura passaria na frente do `player`. Mesma armadilha do workqueue do LVGL |
| `CONFIG_THREAD_ANALYZER_RUN_UNLOCKED` | `y` | Já é o padrão. Desligado, a varredura de todas as pilhas roda com interrupções travadas, e o tick da próxima nota espera por ela |
| `CONFIG_THREAD_ANALYZER_ISR_STACK_USAGE` | `y` | Já é o padrão. É a única medição da pilha em que o callback das notas roda |
| `CONFIG_LOG_BUFFER_SIZE` | `4096` | Ver abaixo |

**O buffer de log.** Com `CONFIG_LOG_PRINTK=y`, o `printk` do analisador
passa pelo log diferido. A thread do analisador (prioridade 10) escreve o
bloco inteiro antes de a thread de log (14) rodar, então todas as linhas têm
de caber no buffer ao mesmo tempo. Com o padrão de 1024 B, as ~13 linhas de
hoje encostam no teto — é **estimativa**, de ~70-80 B por mensagem, não
observação —, e cada thread nova acrescenta uma. Pior: com
`LOG_MODE_OVERFLOW` o log descarta as mensagens **mais antigas**, ou seja, o
começo do relatório, sem outra pista além de uma linha `messages dropped`.
São 3 KB de RAM estática em 520 KB. O `scripts/stack_report.py` avisa se
encontrar descarte no console.

**A prioridade não sai no relatório do analisador.** A opção
`CONFIG_THREAD_ANALYZER_PRINT_THREAD_PRIORITY`, que uma versão anterior do
plano listava, **não existe no Zephyr v4.4.2** — e atribuir símbolo
inexistente reprova o build (`error: Aborting due to Kconfig warnings`,
conferido). A prioridade vem do
`kernel thread list` do shell, que o script também lê.

## Inventário: o que existe hoje

Tamanhos tirados do `zephyr.elf` compilado sobre `825d46f`, não do Kconfig: é
o que o linker reservou de fato. Total: **16 704 B** de pilha, 3,1% da SRAM.

| Pilha | Prioridade | Declarado (B) | Onde se ajusta | O que roda nela |
|---|--:|--:|---|---|
| interrupção | — | 2048 | `CONFIG_ISR_STACK_SIZE` | Todas as ISRs, aninhadas. **O callback do `k_timer` que dispara cada nota** |
| `sysworkq` | -1 (coop.) | 1024 | `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE` | Trabalho diferido dos drivers |
| `main` | 0 | 1024 | `CONFIG_MAIN_STACK_SIZE` | Bring-up: **a primeira montagem do cartão**, a varredura e a carga de cada faixa |
| `lvgl` | 10 | 4096 | `CONFIG_LV_Z_LVGL_WORKQUEUE_STACK_SIZE` | Laço do LVGL, desenho, envio por I²C ao SSD1306 |
| `thread_analyzer` | 10 | 1024 | `CONFIG_THREAD_ANALYZER_AUTO_STACK_SIZE` | A própria varredura |
| `card` | 12 | 2048 | `CARD_MONITOR_STACK_SIZE` (`src/card.c`) | Sondagem, montagem e desmontagem: FatFs mais a pilha SD inteira |
| `logging` | 14 | 2048 | `CONFIG_LOG_PROCESS_THREAD_STACK_SIZE` | Formatação diferida do log |
| `shell_uart` | 14 | 2048 | `CONFIG_SHELL_STACK_SIZE` | Shell, inclusive `lvgl stats memory` e os comandos de pilha |
| `input` | 14 | 1024 | `CONFIG_INPUT_THREAD_STACK_SIZE` | Os callbacks do subsistema `input`, incluindo o do encoder do LVGL |
| `idle` | 15 | 320 | `CONFIG_IDLE_STACK_SIZE` | Nada; só recebe quadro de exceção |

As threads `player_thread` (prioridade 4, 2048 B) e `loader_thread`
(prioridade 8, 4096 B) já existem, mas **só no simulador**: na placa falta a
saída PWM, e sem ela nenhuma das duas entra no build. O `KNOBS` de
`scripts/stack_report.py` já tem as linhas delas, para a tabela sair certa no
dia em que entrarem. A `ui` ainda não existe; quando nascer, entra na tabela
sozinha — o script lê o que o console mostrar —, mas precisa da sua linha no
`KNOBS`, senão a coluna "Onde se ajusta" sai com `?`. **A tabela final só vale
medida sobre o firmware da E6**, com a arquitetura inteira de pé.

### O que olhar primeiro

São hipóteses de leitura do código, não medições:

1. **`main` com 1024 B.** É o padrão do Zephyr, e hoje o `main` faz a
   primeira montagem do cartão (`card_init` chama `card_refresh` antes de
   criar o monitor), que atravessa o FatFs e a pilha SD — o mesmo caminho para
   o qual o monitor do cartão reservou 2048 B. Se alguma pilha sair apertada,
   a aposta é esta. Com a tela (PR #41), o `main` passou a montar também a
   interface e a varrer a biblioteca duas vezes, e o `prj.conf` o subiu para
   4096 B — teto provisório, à espera da medição.
2. **`input` na prioridade 14.** Sem `CONFIG_INPUT_THREAD_PRIORITY_OVERRIDE`,
   a thread do subsistema `input` fica na menor prioridade de aplicação —
   abaixo do `card` (12) e do LVGL (10). Não é assunto de pilha, mas a tabela
   o expõe: a arquitetura põe a tradução de botões em "média-alta", e um
   callback do `input` roda na prioridade dessa thread. Fica para a issue do
   módulo `input`.
3. **A pilha de interrupção.** É a única cujo estouro atinge diretamente a
   tese do trabalho, porque é onde a nota é disparada. Medir com o encoder
   girando, o I²C ocupado e o cartão sendo lido ao mesmo tempo, para que as
   interrupções se aninhem.

## A regra de folga

Para cada pilha, sobre a marca d'água medida:

| Veredito | Condição | Ação |
|---|---|---|
| **ESTOURO** | folga zero | A marca chegou ao fim; o número medido é só um piso. Aumentar e medir de novo |
| **apertada** | folga < máx(25% da pilha, 128 B) | Aumentar |
| sobra | folga > 1024 B **e** uso < 50% | Candidata a encolher; não reprova |
| ok | o resto | Nada |

**Por que 25%.** A marca d'água é o pior caso dos caminhos que rodaram. Os
25% são o espaço para o que o roteiro não provocou: um caminho de erro com
log, uma sequência de comandos de shell que ninguém digitou, uma combinação
de falhas. É uma margem de engenharia, não um número derivado — e está
escrita aqui para poder ser contestada.

**Por que 128 B no mínimo.** Para as pilhas pequenas, 25% é pouco. O piso
cobre uma interrupção chegando exatamente no ponto mais fundo da pilha, que a
medição pode não ter flagrado: o Cortex-M33 com FPU empilha **na pilha da
thread interrompida** um quadro estendido de 104 B (8 palavras básicas mais
18 de ponto flutuante), mais até 4 B de alinhamento a 8. O handler em si roda
na pilha de interrupção.

**Por que "sobra" não reprova.** Pilha grande demais custa RAM, não
correção, e há 520 KB. Encolher uma pilha é decisão de relatório — o número
medido mais a folga da regra —, não de portão.

Os números da regra moram no topo de `scripts/stack_report.py`. Mudar lá sem
mudar aqui deixa este documento mentindo.

## Roteiro de medição

A marca d'água só registra o que aconteceu, então a sessão tem de passar por
todos os caminhos caros de propósito, inclusive os de falha.

1. Compilar e gravar: `scripts/build.sh`, depois `build-out/zephyr.uf2` por
   USB em modo BOOTSEL.
2. Abrir o console serial (115200 8N1) **registrando em arquivo** desde antes
   do reset — o pico do boot conta.
3. Exercitar, nesta ordem:
   - ligar **sem** cartão; inserir o cartão; esperar a biblioteca;
   - uma biblioteca **cheia**, com mais de 32 arquivos e ao menos um inválido
     e um maior que 4 KB;
   - percorrer a lista inteira com o encoder, ida e volta;
   - tocar uma faixa até o fim; pausar e retomar; seguinte e anterior;
   - tocar com o encoder girando sem parar, para aninhar interrupções;
   - **remover o cartão tocando**; reinserir;
   - no shell: `lvgl stats memory`.
4. Ainda no shell, por último: `kernel thread stacks` e `kernel thread list`.
   O primeiro dá todas as pilhas mais a de interrupção; o segundo dá a
   prioridade.
5. Gerar a tabela:

   ```bash
   python3 scripts/stack_report.py --check console.log
   ```

   Sai com 1 se alguma pilha estiver apertada ou estourada. Avisa se o
   console acusar descarte de log, ou se faltar a prioridade de alguma
   thread.

6. Colar a tabela abaixo, com o commit medido, e justificar cada pilha que
   for mudar de tamanho.

## Resultado

> **Pendente.** Preencher com a saída de `scripts/stack_report.py` sobre o
> console da sessão do roteiro, na placa, com o firmware da E6.

| Thread | Prioridade | Pilha (B) | Pico (B) | Folga (B) | Uso | Onde se ajusta | Veredito |
|---|--:|--:|--:|--:|--:|---|---|
| — | | | | | | | |

**Commit medido:** — · **Data:** —

# Vitrolinha — tocador de músicas embarcado

Firmware em C sobre **Zephyr RTOS** para o kit **ZBook** (RP2350B, Arm Cortex-M33
duplo, 520 KB SRAM), board out-of-tree em https://github.com/zephyr-book/zbook.

Trabalho da disciplina de Projetos de Sistemas Embarcados. A especificação
completa está em `docs/especificacao-vitrolinha.md`.

## O que o sistema faz

Lista os arquivos musicais do cartão microSD em um menu no display OLED,
navegável pelo encoder rotativo. A faixa selecionada é reproduzida pelo buzzer da
placa via PWM, com nome, tempo decorrido e barra de progresso na tela. Os botões
controlam tocar/pausar, faixa seguinte e anterior.

**O áudio é monofônico e sintetizado**, não gravado. Isso é escopo, não bug.

## Alvo de compilação — leia antes de qualquer coisa

```
west build -b zbook@p2/rp2350b/m33
```

**O sufixo `@p2` é obrigatório.** O encoder, o nó do display, o `chosen
zephyr,display` e o card-detect existem **somente** na revisão P2. O `board.yml`
declara `default: p2`, mas esse campo é ignorado em boards de formato `custom`, e
`revision.cmake` cai em `p1` quando `BOARD_REVISION` não vem definido. Compilar
sem o `@p2` produz um binário para uma placa sem encoder e sem tela, **sem erro
nenhum**.

O P2 também resolve conflitos de pino que o P1 tem (GPIO 14/15 disputados entre
`spibb0` e `spi1`, GPIO 29 entre `ir_emitter` e `UART0_RX`).

## Hardware — verificado no devicetree

| Recurso | Nó / alias | Pinos | Observação |
|---|---|---|---|
| Buzzer **piezo passivo** | `pwm_buzzer` / `pwm-buzzer` | GPIO 32 | PWM **slice 8 canal A**, via MOSFET Q7. A placa fixa `divider-int-8 = <255>`; **a aplicação sobrescreve para modo automático** — ver a seção de síntese |
| OLED **SSD1306** 128x64 | `oled`, `chosen zephyr,display` | I²C0 @ 0x3c, SDA GPIO 4, SCL GPIO 5 | **Não é SH1106.** Descrito como SH1106 ele confirma no I²C e aceita tudo, mas a bomba de carga não liga e a tela fica preta sem erro |
| Encoder PEC12R | `encoder_qdec` (`gpio-qdec`) | A GPIO 42, B GPIO 43 | `INPUT_REL_WHEEL`, `steps-per-period = <4>`, acorda por interrupção |
| Clique do encoder | `enc_button` / `enc-button` | GPIO 38 | `gpio-keys`, `INPUT_KEY_ENTER` |
| Botões 0-3 | `button0`..`button3` | GPIO 23, 22, 19, 18 | `INPUT_BTN_0..3`, ativo-baixo. **Sem serigrafia no P2, e a ordem física não segue a numérica** |
| microSD | `sdhc0` (`zephyr,sdhc-spi-slot`) | SPI0: SCK 34, MOSI 35, MISO 36, CS 37 | disco `"SD"`, 24 MHz. Barramento separado do IMU (SPI1) |
| Detecção de cartão | `sd_cd` (`edge,gpio-inputs`) | GPIO 33 | ativo-baixo, pull-up externo de 10k. Nenhum driver liga nele por padrão — **a aplicação o liga à pilha SD por `cd-gpios`** |
| Potenciômetro | `potentiometer` `channel@1` | GPIO 41 (ADC1) | fora do MVP |
| LEDs 0-3 | `led0`..`led3` | GPIO 24-27 | GPIO puro, ativo-baixo. **Um deles é o pino de instrumentação da E6** |
| LEDs RGB | `ws2812`, 4 elementos | GPIO 31 | via **PIO1**, não PWM — não compete com o buzzer |

O `pico_header` do P1 é apagado no P2 e nada o substitui: **o conector de
expansão do P2 não está modelado em nenhum lugar do repositório**, e o
`zbook-p2-schematics.pdf` não está publicado. Por isso a instrumentação de tempo
mede num terminal de LED, não num pino de header.

### Overlay da aplicação

```dts
/* Modo automático de divisor no slice do buzzer: o driver passa a escolher o
 * divisor por potência de dois a cada nota, mantendo o contador acima de 32768.
 * Com o divisor fixo em 255 da placa, o erro de afinação chega a ~6 cents na
 * oitava 7 — justamente onde a transposição para cima leva a melodia.
 */
&pwm {
	/delete-property/ divider-int-8;
};

/* 100 kHz custam ~92 ms por quadro cheio de 128x64. */
&i2c0 {
	clock-frequency = <I2C_BITRATE_FAST>;
};

/* Sem cd-gpios, sdhc_spi_get_card_present devolve 1 incondicionalmente:
 * "SPI has no card presence method, assume card is in slot".
 */
&sdhc0 {
	cd-gpios = <&gpio0_hi 1 GPIO_ACTIVE_LOW>;
};

/ {
	lvgl_encoder_input {
		compatible = "zephyr,lvgl-encoder-input";
		rotation-input-code = <INPUT_REL_WHEEL>;
		button-input-code = <INPUT_KEY_ENTER>;
	};
};
```

Acrescentar `debounce-interval-ms = <20>` explícito nos nós de botão, para não
depender de padrão herdado.

### Kconfig que o defconfig da placa NÃO dá

O defconfig já traz `GPIO, I2C, SPI, ADC, SERIAL, PINCTRL, CLOCK_CONTROL, LOG,
FPU, LED_STRIP, SDHC, DISK_ACCESS, DISK_DRIVERS, FILE_SYSTEM,
FAT_FILESYSTEM_ELM`. Falta acrescentar:

```
CONFIG_PWM=y
CONFIG_PWM_RPI_PICO=y
CONFIG_INPUT=y
CONFIG_INPUT_GPIO_QDEC=y
CONFIG_INPUT_GPIO_KEYS=y
CONFIG_DISPLAY=y
CONFIG_SSD1306=y

CONFIG_SYS_CLOCK_TICKS_PER_SEC=20000         # 50 us de quantização; ver temporização

CONFIG_LVGL=y
CONFIG_LV_COLOR_DEPTH_1=y
CONFIG_LV_Z_MONOCHROME_CONVERSION_BUFFER=y   # obrigatório: sem isso, mono cai em -ENOTSUP
CONFIG_LV_Z_COLOR_MONO_HW_INVERSION=y
CONFIG_LV_Z_VDB_SIZE=100                     # quadro inteiro; ~2064 B com o buffer de conversão
CONFIG_LV_Z_MEM_POOL_SIZE=24576              # o padrão de 2048 não sustenta uma lista de 32 itens
CONFIG_LV_Z_LVGL_WORKQUEUE_PRIORITY=10       # o padrão é 0 e sobrepujaria o player
CONFIG_LV_Z_SHELL=y                          # `lvgl stats memory`, para dimensionar o pool por medição
CONFIG_LV_FONT_UNSCII_8=y
CONFIG_LV_FONT_DEFAULT_UNSCII_8=y

CONFIG_SD_DATA_TIMEOUT=500                   # o padrão é 10000 ms x 3 tentativas
CONFIG_SD_DATA_RETRIES=1
CONFIG_FS_FATFS_NUM_FILES=2                  # o padrão de 4 é pré-alocado estaticamente
CONFIG_FS_FATFS_NUM_DIRS=2

CONFIG_THREAD_ANALYZER=y                     # critério de avaliação: pilha por thread
CONFIG_THREAD_ANALYZER_AUTO=y
CONFIG_THREAD_ANALYZER_PRINT_THREAD_PRIORITY=y
CONFIG_THREAD_ANALYZER_AUTO_THREAD_PRIORITY_OVERRIDE=y
CONFIG_THREAD_ANALYZER_AUTO_THREAD_PRIORITY=10   # o padrão também é 0
```

**`CONFIG_LV_Z_LVGL_WORKQUEUE_PRIORITY` tem padrão 0**, a maior prioridade
preemptível. Deixado assim, o laço do LVGL **sobrepuja qualquer thread do
player** — o inverso exato da arquitetura pretendida. É a única configuração
desta lista que, omitida, invalida a tese do trabalho sem dar erro nenhum.

O `CONFIG_FS_FATFS_LFN` fica **desligado** (ver a decisão sobre nome exibido), o
que também dispensa decidir entre `LFN_MODE_BSS` e reentrância: `REENTRANT`
depende de `!LFN_MODE_BSS`, e com reentrância ligada o `FF_FS_TIMEOUT` do FatFs é
`K_FOREVER`. Como só o `storage` toca no sistema de arquivos, reentrância não é
necessária.

O manifesto west precisa de `fatfs` mesmo se o cartão não fosse usado, porque o
defconfig da placa liga `FAT_FILESYSTEM_ELM` incondicionalmente. Lista:
`hal_rpi_pico, cmsis_6, lvgl, littlefs, fatfs`.

## Decisões já tomadas (e por quê)

- **Buzzer como saída de áudio.** Sem hardware externo. O RP2350 não tem DAC, e a
  placa não tem codec. A alternativa está registrada como evolução futura na
  seção 10 da espec, fora do escopo atual.
- **RTTTL como formato dos arquivos.** Textual, arquivos de poucos KB, e o parser
  é escrito do zero. Um segundo formato próprio, CSV de `frequência,duração_ms`,
  existe para testes de bancada e para o arquivo de calibração.
- **LVGL no OLED**, profundidade de 1 bit, com o indev nativo de encoder e grupo
  de foco — não navegação manual.
- **Volume fora do MVP.** No buzzer o volume depende do duty cycle de forma não
  linear e com faixa útil estreita: muito esforço de calibração, pouco resultado.
- **Núcleo único, sem alternativa.** O Zephyr **não tem porte SMP para
  Cortex-M** — a única implementação em `arch/arm/` é para Cortex-A/R, e a
  documentação upstream do RP2350 diz que roda em um núcleo só. Consequência: a
  regra "o `player` nunca bloqueia" é estrutural, não redundância. Não há núcleo
  de reserva.
- **O arquivo é carregado inteiro em RAM antes de tocar.** Buffer estático de
  4 KB. Um RTTTL típico tem 100-400 bytes; 4 KB cobrem ~1000 notas. Efeitos: o
  RNF06 é satisfeito por construção, a reprodução **não toca no cartão nem uma
  vez**, o RNF07 sai quase de graça, e reiniciar a faixa é reinterpretar do byte
  zero. Streaming durante a reprodução foi rejeitado: pagaria a complexidade *e*
  ficaria com o risco.
- **Transposição por oitavas quando necessário.** Um piezo ressoa perto de 4 kHz
  e é fraco abaixo de ~500 Hz, então notas graves saem audivelmente mais fracas
  e a melodia fica com volume irregular. O `parser` desloca a faixa inteira em
  oitavas inteiras até a nota mais grave passar do limiar, preservando os
  intervalos. **O limiar sai de medição na E1**, com a varredura de frequência da
  bring-up do fabricante — não de folclore.
- **O nome exibido vem do cabeçalho RTTTL**, não do nome de arquivo. Sem
  `CONFIG_FS_FATFS_LFN` o `readdir` devolve 8.3 em maiúsculas (`FURELIS.TXT`), o
  que é ruim numa tela cujo único texto é o nome da faixa. Ler o cabeçalho evita
  o custo de RAM do LFN. Com a fonte UNSCII-8 a tela tem grade exata de 16
  colunas por 8 linhas, então o nome útil tem **16 caracteres**.
- **Biblioteca de 32 faixas no máximo**, nomes de 24 bytes: 768 B fixos. Arquivos
  além do 32º são ignorados com aviso na tela. Comportamento definido, não
  estouro.
- **`lv_list`, não `lv_table`.** No LVGL 9 cada item de lista é um botão mais um
  rótulo, ~150-200 B de heap cada: 32 faixas custam 16-24 KB de pool. O
  `lv_table` custaria quase nada, mas **não tem foco por linha**, o que jogaria
  fora a navegação nativa por grupo. Em 520 KB de SRAM, 24 KB por navegação
  nativa é barato — e o teto de 32 faixas mantém o consumo fixo, que é o que o
  RNF06 exige. Dimensionar por medição com `lvgl stats memory`, não por
  estimativa.

## Controles

O encoder navega, os botões controlam o transporte. Regra de uma linha:
**transporte é global e independente da tela; o encoder é sempre navegação.**

| Controle | Ação |
|---|---|
| Girar encoder | Move o foco na biblioteca |
| Clicar encoder | Seleciona = **toca imediatamente**, substituindo a faixa atual |
| Botão físico 1 | Faixa anterior |
| Botão físico 2 | Tocar / pausar |
| Botão físico 3 | Faixa seguinte |
| Botão físico 4 | **Alterna** biblioteca ↔ reprodução |

O botão 4 alterna em vez de só voltar, porque a espec pedia "voltar ao menu" e
não previa o caminho de volta — com os quatro botões ocupados, não existia
controle para reabrir a tela de reprodução. Alternar é superconjunto, custo zero.

Não há fila de reprodução: selecionar troca a faixa. Na biblioteca, o item que
está tocando leva um marcador `>`, e um arquivo rejeitado pelo parser leva `!`.

**A ordem física dos botões é tarefa da E1.** Os GPIOs são 23, 22, 19, 18 nessa
ordem de alias, o P2 não tem serigrafia, e a ordem física não segue a numérica.
O mapeamento acima é por posição física, nunca por número de alias.

No fim da faixa o player **para e permanece na tela de reprodução**, com o
progresso em 100%; o botão de tocar reinicia do começo. Isso deixa o avanço
automático (RF09) como troca de uma linha na E7 — em vez de "parar", "carregar a
próxima".

## Temporização — a parte que sustenta o RNF01 e o RNF02

### A grade

Calcular a duração de cada nota em ms por divisão inteira e **somar** acumula
~0,5 ms de arredondamento por nota: em 300 notas, até 150 ms. Passaria no RNF01
de 1%, mas por sorte, e invalidaria a tese do trabalho.

Em vez disso o `parser` mantém um **contador inteiro de subdivisões** `n64` — o
número de sexagésimas-quartas desde o início da faixa — e converte cada instante
com uma única conta em 64 bits:

```c
offset_us = (uint64_t)n64 * 60000000ULL * 4 / (bpm * 64);   /* = n64 * 3750000 / bpm */
```

O erro passa a ser truncamento único e **não acumulativo**. O RNF01 fica
satisfeito por construção, não por medição — e isso é item de relatório.

A subdivisão base é 1/64 e não 1/32 porque uma semifusa pontuada (`d=32.`) vale
1,5 trigésimas-segundas, que não é inteiro. Em sexagésimas-quartas toda duração
do RTTTL cai em inteiro: `n64 = 64/d`, e `96/d` quando pontuada. Se o "RTTTL
estendido" um dia admitir quiálteras, a base precisa virar múltiplo de 3.

A duração total da faixa é o `offset_us` do último evento — obtida na mesma
passagem que preenche a fila, sem varredura extra.

### Quem dispara a nota

**O callback de expiração de um `k_timer`, em contexto de interrupção.** O
`pwm_set_cycles` no rp2xxx é seguro em interrupção: sem mutex, sem semáforo, sem
log, sem dormir — são escritas em registrador e leituras de configuração em ROM.
O callback faz só quatro coisas: ajusta o tom, tira o próximo evento da fila,
reagenda para o instante absoluto seguinte e atualiza o instantâneo. A thread
`player` mantém a máquina de estados e é acordada apenas por eventos: fila baixa,
fim da faixa, comando do usuário.

O ganho não é só de jitter — 1 a 3 µs de latência de interrupção contra dezenas
de µs de um despertar de thread. É que **o disparo deixa de depender do
escalonador por completo**, o que é uma afirmação bem mais forte no relatório do
que "as prioridades estão certas".

Reagendar com `K_TIMEOUT_ABS_TICKS`, nunca `k_sleep(duração)`. Detalhe do kernel
que ajuda: dentro de um callback de timeout, `k_uptime_ticks()` devolve o
instante **agendado**, não o real, justamente para que o próximo prazo seja
calculado a partir de onde o evento deveria ter ocorrido.

**Não usar a API `counter`.** O `COUNTER_ALARM_CFG_ABSOLUTE` está quebrado no
driver do rp2xxx: descarta o `ticks` pedido, dispara imediatamente e devolve
`-ETIME`. Só alarmes relativos funcionam, o que reintroduz a deriva.

Tick em **20 kHz** (`CONFIG_SYS_CLOCK_TICKS_PER_SEC=20000`): 50 µs de
quantização, de graça sob tickless. **Não subir para 100 kHz** — nesse ponto o
ciclo por tick iguala o atraso mínimo do SysTick e o driver pode expirar em
dobro.

### O que atravessa a fila

A fila carrega **eventos, não notas**:

```c
struct evt { uint32_t offset_us; uint32_t freq_hz; };  /* freq_hz == 0 => silêncio */
```

Cada nota vira dois eventos — o tom em `offset_us`, e o silêncio da articulação
em `offset_us + tone_us`. O callback fica uniforme, sem ramificação entre "ligar"
e "desligar". Fila de **32 eventos** = 16 notas = 256 B.

Toda a aritmética de andamento vive no `parser`; o `player` e o callback só fazem
`deadline = base_us + offset_us`. Auditar o RNF01 é revisar uma função.

### Articulação

Notas iguais consecutivas se fundem num tom só se não houver silêncio entre
elas. Cada nota soa por `duração - min(duração/10, 20 ms)` e silencia o resto da
fatia. O silêncio fica **dentro** da fatia, para não perturbar a grade. Pausa
RTTTL (`p`) é fatia inteira em silêncio.

**Silenciar é zerar o pulso, nunca o período** — período zero é ajuste inválido
de PWM no Zephyr.

### Pausa

A base temporal não sobrevive fixa a uma pausa: ela desliza. Ao pausar, silencia
e guarda o instante. Ao retomar, `base_us += (agora - início_da_nota)`, ou seja,
a nota corrente recomeça inteira. Uma única variável conserta ao mesmo tempo o
agendamento e o tempo decorrido. Custo aceito: a barra de progresso anda para
trás menos de uma nota ao retomar.

## Síntese

O período do PWM **sempre** sai de `pwm_get_cycles_per_sec()`, nunca de constante
escrita à mão, e a chamada é `pwm_set_cycles`, não `pwm_set` — esta última
converte ns para ciclos com truncamento, pagando arredondamento duas vezes.

Isso não é preciosismo. O driver tem dois modos: com `divider-int-N` presente ele
opera em **modo fixo** e não reescala o divisor, e nesse caminho **não valida o
limite de 16 bits**. Com o `divider-int-8 = <255>` da placa, o `period_cycles`
está em unidades de 588 kHz; calcular `150000000/freq` por hábito produz 340909
para o Lá 440, o driver escreve `wrap = 340908`, o registrador trunca e sai
**44,5 Hz** — nota errada, sem erro, sem aviso.

O overlay sobrescreve para modo automático, o que também melhora a afinação de
~6 cents para ~0,05 cent no topo da faixa. Mas a regra de derivar de
`pwm_get_cycles_per_sec()` vale nos dois modos, e é ela que evita a armadilha.

## Arquitetura

Módulos com fronteiras estanques. O objetivo é que nenhuma operação gráfica ou de
cartão possa atrasar a próxima nota.

| Módulo | Responsabilidade |
|---|---|
| `player` | Máquina de estados: parado / tocando / pausado. Arma o timer, não dispara a nota |
| `synth` | Nota+oitava -> frequência -> PWM. **Único módulo que conhece o buzzer** |
| `parser` | Interpretação do RTTTL a partir do buffer em RAM, grade `n64`, duração total, transposição |
| `storage` | Montagem do cartão, listagem da biblioteca, leitura do arquivo para o buffer, presença do cartão |
| `ui` | Telas LVGL, máquina de estados de navegação |
| `input` | Botões de transporte pelo subsistema `input`, virando comandos em fila |
| `volume` | Leitura filtrada do ADC (opcional, pós-MVP) |

### Threads

| Thread | Prioridade | Papel |
|---|---|---|
| callback do `k_timer` | interrupção | **Dispara cada evento.** Ajusta o tom, reagenda, atualiza o instantâneo |
| `player` | Alta | Máquina de estados. Acordado por comando, fila baixa ou fim de faixa. Nunca faz E/S de cartão, nem I²C, nem gráficos |
| `input` | Média-alta | Traduz eventos em comandos e publica em fila |
| `ui` | Baixa | Laço do LVGL em intervalo fixo, redesenho por região suja |
| `loader` | Baixa | Lê o arquivo para o buffer, interpreta e preenche a fila de eventos |

O `loader` interpreta **do buffer em RAM**, não do cartão.

### Quem é o dono do relógio

A UI precisa do tempo decorrido sem interrogar o `player` a cada quadro. A
solução é um **registro instantâneo de escritor único**: `{estado, base_us,
total_us, faixa}`, escrito só pelo lado do player e lido sem bloqueio pela `ui`,
que calcula `decorrido = agora - base_us`.

Isso emenda a regra antiga de "sem estado global compartilhado", que era
incompatível com o próprio requisito da barra de progresso. A regra correta é:
**sem estado mutável compartilhado sem disciplina de propriedade — escritor
único, leitura sem bloqueio.** Todo o resto é `k_msgq`.

## Interface

**O overlay não conecta o grupo de foco.** O `lvgl_input_register_driver` só faz
`lv_indev_create`, `lv_indev_set_type(LV_INDEV_TYPE_ENCODER)`, `set_read_cb` e
`set_display`. Criar o grupo e amarrar é da aplicação: `lv_group_create()`,
`lv_indev_set_group(indev, grupo)`, `lv_group_add_obj(...)`, com o indev obtido
por `lvgl_input_get_indev(DEVICE_DT_GET(DT_NODELABEL(lvgl_encoder_input)))`. É o
tropeço mais comum dessa integração: o encoder gera eventos, o LVGL os recebe, e
nada se move na tela porque não há grupo.

Sem phandle `input`, o nó assina todos os dispositivos de entrada e guarda só os
dois códigos declarados, **ignorando os quatro botões** — exatamente a divisão
desejada: encoder para o grupo de foco, botões para a fila de comandos própria.

**O I²C a 100 kHz limita a taxa de quadros.** Um quadro cheio de 128x64 a 1 bpp
são 1024 bytes, ~92 ms no barramento — os 10 fps do plano não cabem em quadro
cheio. O overlay sobe para 400 kHz, o que leva para ~23 ms. Ainda assim, desenhar
por região suja é o que importa: mudar o rótulo de tempo e a barra de progresso
são algumas centenas de bytes, não 1024. O quadro cheio só acontece na troca de
tela.

Isso **não** ameaça a temporização das notas: a `ui` é de baixa prioridade e
preemptível, o `player` nunca toca no I²C, e o disparo da nota está em contexto de
interrupção. Ameaça só a fluidez da transição de tela. Medir antes de fixar a
taxa.

O alinhamento de página do SSD1306 é resolvido sozinho: o driver reporta
`SCREEN_INFO_MONO_VTILED` e o `lvgl_rounder_cb_mono` arredonda as áreas para
múltiplos de 8 em Y.

## Falhas

Três falhas distintas, com comportamento definido para cada.

| Falha | Comportamento |
|---|---|
| Sem cartão no boot | Tela de erro "Sem cartao". A presença é detectada pela pilha SD via `cd-gpios`; na inserção, tenta montar e varrer |
| Cartão removido tocando | **A música termina**, porque o arquivo já está inteiro em RAM e nem o `player` nem o callback tocam no sistema de arquivos. A biblioteca é marcada inválida, seguinte/anterior são desabilitados, e ao voltar ao menu aparece a tela de erro |
| Arquivo inválido | O `parser` rejeita, tela de erro com o motivo por ~2 s, volta à biblioteca, item marcado com `!` |

**Remontagem a quente não é confiável e não deve ser prometida.** A issue #94033
do Zephyr, "SD card hot-plug not supported", está **fechada como "not
planned"**: `fs_mount()` devolve `-EIO` na reinserção, com atrasos de 14+
segundos relatados na tentativa de desmontar. A aplicação **tenta**
`fs_unmount`/`fs_mount` na reinserção porque custa pouco e às vezes funciona, e
cai para "reinicie a placa" quando falha. O RNF07 é redigido como "sem
travamento", que é o que a espec sempre disse, e não "com recuperação", que ela
nunca prometeu.

Os timeouts padrão do subsistema SD são **10 s vezes 3 tentativas**, o que faria
um único `fs_read` bloquear por dezenas de segundos. Cortados para 500 ms e 1
tentativa no `prj.conf`.

## Restrições a respeitar no código

- Nenhuma chamada de sistema de arquivos, de I²C ou de LVGL na thread `player`
  nem no callback do timer.
- No callback do timer, só `pwm_set_cycles`, `k_msgq_get` com `K_NO_WAIT`,
  `k_timer_start` e escrita no instantâneo. Nada que durma, registre log ou
  aloque.
- O redesenho da UI é limitado por taxa fixa e nunca dispara por nota.
- Buffers de tamanho fixo: o uso de RAM não depende do tamanho do arquivo.
- Escrever o parser RTTTL, não importar biblioteca pronta — é entregável do
  trabalho.
- Período de PWM sempre derivado de `pwm_get_cycles_per_sec()`. Nunca constante.
- Silêncio é pulso zero, nunca período zero.
- Nunca usar índice de canal de ADC fixo no código: o P1 e o P2 numeram
  diferente. Usar `DT_NODELABEL(potentiometer)`.
- ADC do potenciômetro precisa de média móvel e zona morta, senão o volume
  oscila sozinho.
- Não escrever no cartão durante a reprodução: o `sdhc_spi_wait_unbusy` tem um
  laço de espera sem decremento no caminho de escrita.

## Riscos

**Fechados:**

1. ~~O buzzer é passivo ou ativo?~~ **Passivo (piezo).** O devicetree do P2 diz
   `BUZZER_GPIO (piezo, Q7)`, e a bring-up do fabricante em
   `zephyr-book/samples` varia o *período* para gerar 440/880/1760/3520 Hz,
   comentando que o piezo é mais alto a 50% de ciclo de trabalho. Citar a
   bring-up no relatório, não o devicetree — o comentário sozinho é evidência
   mais fraca.
2. ~~O PWM do buzzer compartilha slice com outro periférico?~~ **Não.** O slice 8
   é exclusivo do buzzer. Os LEDs 0-3 são GPIO puro, a fita RGB usa PIO1, e o
   único outro consumidor de PWM é o motor no slice 3B. Importa porque canais A e
   B de um slice **compartilham TOP e divisor**: um LED em PWM no canal irmão
   mudaria a afinação. Latentes: GPIO 33 e 41 também mapeiam para 8B, mas estão
   muxados como GPIO e ADC.
3. ~~O microSD está no mesmo barramento SPI do header?~~ O cartão está no SPI0 e
   o IMU no SPI1, barramentos separados. Se o header do P2 toca o SPI0 é
   **indeterminável**: o `pico_header` foi apagado e o esquemático não está
   publicado. No P1 não tocava.

**Abertos:**

4. Frequência de ressonância do piezo — nenhum número de peça em nenhum
   repositório. Define o limiar de transposição. **Medir na E1.**
5. O driver de PWM não zera o contador ao trocar de nota, então o TOP é
   reprogramado no meio de um ciclo. Se isso produz um período audivelmente
   sujo na troca de nota é **teste de bancada da E1** — a ausência da chamada
   está confirmada no código, a consequência audível não.
6. Existem pull-ups externos nos botões e nas linhas A/B do encoder? Nenhum dos
   nós tem `GPIO_PULL_UP`, mas o sensor hall no mesmo arquivo tem o flag com o
   comentário de que não há pull-up externo ali — o que implica que há nos
   outros. Se o encoder ler lixo, acrescentar `GPIO_PULL_UP` no overlay.
7. O que o conector de expansão do P2 expõe. GPIO 1, 12, 13, 30, 39, 45 e 46
   estão livres no devicetree, mas não se sabe quais saem na placa. Pedir o
   `zbook-p2-schematics.pdf` ao professor fecha 4, 6 e 7 de uma vez.

## Metas mensuráveis (requisitos não funcionais)

- Erro de tempo acumulado em 60 s de melodia: menor que 1%.
- Desvio de início de cada nota: menor que 5 ms.
- Latência botão -> efeito audível: menor que 100 ms, **incluindo seguinte e
  anterior**, que exigem abrir e interpretar outro arquivo. Medir na E1; a
  pré-carga das faixas vizinhas em buffers extras (12 KB, ainda fixos) só entra
  se a medição exigir.
- Atualização da UI sem violar o desvio de nota acima.
- Uso de RAM independente do tamanho do arquivo.
- Sem travamento se o cartão for removido durante a reprodução.

Instrumentar chaveando um dos LEDs 0-3 (GPIO 24-27) e medindo no terminal do LED,
já que não há pino de header confirmado. Manter na biblioteca um arquivo de
calibração no formato CSV com N notas idênticas de duração conhecida. Usar
`CONFIG_THREAD_ANALYZER` para o consumo de pilha por thread, que é critério de
avaliação — e `k_cycle_get_64()`, não a versão de 32 bits, que dá a volta a cada
28,6 s a 150 MHz e arruinaria uma medição de 60 s.

## Escopo em etapas

- **E0** Ambiente: workspace west com a board out-of-tree, git inicializado,
  build para `zbook@p2/rp2350b/m33` piscando um LED.
- **E1** Validação do hardware: buzzer emitindo notas por PWM; **varredura de
  frequência para achar o limiar de resposta do piezo**; verificação da troca de
  nota quanto a estalo; encoder e botões lidos e **ordem física dos botões
  identificada**; medição de `fs_open` + leitura, para o orçamento do RNF03.
- **E2** LVGL no OLED com menu navegável pelo encoder (lista fixa).
- **E3** Cartão montado e menu populado com os arquivos reais, nomes lidos do
  cabeçalho RTTTL.
- **E4** Reprodução da faixa selecionada com tela de progresso.
- **E5** Tocar, pausar, avançar e retroceder durante a reprodução. — **fim do MVP**
- **E6** Coleta das métricas, relatório e demonstração.
- **E7** Opcionais: volume pelo potenciômetro, persistência de preferências,
  avanço automático de faixa, sinalização por LEDs RGB.

## Ideias já levantadas, ainda não implementadas

- Conversor MIDI -> RTTTL em Python: extrai a trilha melódica, descarta polifonia
  mantendo a nota mais aguda de cada acorde, quantiza durações. Vira ferramenta de
  autoria e entregável extra.
- "RTTTL estendido": permitir arquivos longos, oitavas 3 a 7 e mudança de andamento
  no meio da faixa, documentando o desvio em relação ao formato original.

## Histórico

A proposta original era um **leitor de ebooks**. O professor apontou que a tela de
128x64 (cerca de 20 caracteres por linha, 8 linhas) é impeditiva para leitura de
texto longo. O escopo migrou para o tocador de músicas, preservando o menu de
seleção, a máquina de estados da interface e a leitura do cartão.

A revisão de 2026-09-08 (versão 3 da espec) fechou os três riscos de hardware por
leitura do devicetree, retratou a ideia de usar o segundo núcleo (não existe SMP
em Cortex-M no Zephyr), trocou streaming por carga integral em RAM, corrigiu o
furo aritmético da grade temporal, moveu o disparo da nota para contexto de
interrupção, fechou o buraco de navegação do RF07 e rebaixou a promessa de
recuperação do cartão para o que o Zephyr sustenta. A versão 1 deste arquivo está
em `docs/historico/CLAUDE-v1.md`; a versão 2 da espec, em PDF, foi descartada.

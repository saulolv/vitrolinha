# Jukebuzz — tocador de músicas embarcado

Firmware em C sobre **Zephyr RTOS** para o kit **ZBook** (RP2350B, Arm Cortex-M33
duplo, 520 KB SRAM), board out-of-tree em https://github.com/zephyr-book/zbook.

Trabalho da disciplina de Projetos de Sistemas Embarcados. A especificação
completa está em `docs/especificacao-tocador-musicas-zbook.pdf`.

## O que o sistema faz

Lista os arquivos musicais do cartão microSD em um menu no display OLED,
navegável pelo encoder rotativo. A faixa selecionada é reproduzida pelo buzzer da
placa via PWM, com nome, tempo decorrido e barra de progresso na tela. Os botões
controlam tocar/pausar, faixa seguinte e anterior.

**O áudio é monofônico e sintetizado**, não gravado. Isso é escopo, não bug.

## Hardware disponível na placa

| Recurso | Uso |
|---|---|
| Buzzer | Saída de áudio via PWM; a frequência define a nota |
| OLED 128x64 mono | Interface em LVGL |
| Encoder rotativo | Navegação e seleção no menu |
| BTN0 / BTN1 / BTN2 / BTN3 | Anterior / play-pause / seguinte / voltar ao menu |
| microSD | Biblioteca de faixas |
| Potenciômetro (ADC) | Volume — **fora do MVP** |
| LEDs RGB, LDR, LEDs 0-3 | Realimentação opcional |
| Header de expansão | SPI, I2C, IO3, IO4, IO01, IO45 livres — reservado para evolução |

## Decisões já tomadas (e por quê)

- **Buzzer como saída de áudio.** Sem hardware externo. O RP2350 não tem DAC, e a
  placa não tem codec. A alternativa (PWM+filtro+amp, ou DAC I2S no header) está
  registrada como evolução futura na seção 10 da espec, fora do escopo atual.
- **RTTTL como formato dos arquivos.** Textual, arquivos de poucos KB, e o parser
  é escrito do zero (mapear nota+oitava para frequência e duração). Um segundo
  formato próprio, CSV de `frequência,duração_ms`, existe para testes de bancada.
- **LVGL no OLED**, com profundidade de 1 bit e buffer parcial. LVGL tem suporte
  nativo a encoder como input device com grupos de foco — usar isso, não
  navegação manual.
- **Volume fora do MVP.** No buzzer o volume depende do duty cycle de forma não
  linear e com faixa útil estreita: muito esforço de calibração, pouco resultado
  perceptível.
- **Temporização por instantes absolutos.** O player NÃO dorme pela duração da
  nota. Calcula o instante absoluto de início da próxima a partir de uma base
  temporal fixa, para não acumular erro de tempo ao longo da música.

## Arquitetura

Módulos com fronteiras estanques. O objetivo é que nenhuma operação gráfica ou de
cartão possa atrasar a próxima nota.

| Módulo | Responsabilidade |
|---|---|
| `player` | Máquina de estados: parado / tocando / pausado. Agenda as notas |
| `synth` | Nota+oitava -> frequência -> PWM. **Único módulo que conhece o buzzer** |
| `parser` | Interpretação incremental do RTTTL e cálculo da duração total |
| `storage` | Montagem do cartão, listagem da biblioteca, leitura de arquivos |
| `ui` | Telas LVGL, máquina de estados de navegação |
| `input` | Encoder e botões pelo subsistema `input`, virando comandos de alto nível |
| `volume` | Leitura filtrada do ADC (opcional, pós-MVP) |

### Threads

| Thread | Prioridade | Papel |
|---|---|---|
| `player` | Alta | Dispara cada nota. Nunca faz E/S de cartão nem gráficos |
| `input` | Média-alta | Traduz eventos em comandos e publica em fila |
| `ui` | Baixa | Laço do LVGL em intervalo fixo, ~10 fps, redesenho parcial |
| `loader` | Baixa | Lê e interpreta o arquivo, preenchendo a fila de notas |

Comunicação por `k_msgq`, sem estado global compartilhado. O `player` consome de
uma fila pré-preenchida pelo `loader`, para que uma leitura lenta do cartão não
se manifeste como falha audível.

## Restrições a respeitar no código

- Nenhuma chamada de sistema de arquivos ou de LVGL dentro da thread `player`.
- O redesenho da UI é limitado por taxa fixa e nunca dispara por nota.
- Buffers de tamanho fixo: o uso de RAM não deve depender do tamanho do arquivo.
- Escrever o parser RTTTL, não importar biblioteca pronta — é entregável do
  trabalho.
- ADC do potenciômetro precisa de média móvel e zona morta, senão o volume oscila
  sozinho.

## Riscos abertos — verificar antes de codificar

1. **O buzzer é passivo ou ativo?** Se for ativo, emite frequência fixa e a
   síntese de notas não funciona. Checar o nó no Devicetree da ZBook e o
   esquemático. **Isto é bloqueante.**
2. O canal de PWM do buzzer compartilha slice/timer com os LEDs ou outro
   periférico?
3. O microSD está no mesmo barramento SPI do header de expansão? Importa se um
   dia entrar um DAC ou painel externo.

## Metas mensuráveis (requisitos não funcionais)

- Erro de tempo acumulado em 60 s de melodia: menor que 1%.
- Desvio de início de cada nota: menor que 5 ms.
- Latência botão -> efeito audível: menor que 100 ms.
- Atualização da UI sem violar o desvio de nota acima.
- Uso de RAM independente do tamanho do arquivo.
- Sem travamento se o cartão for removido durante a reprodução.

Instrumentar por pino de depuração para medir a temporização, e manter na
biblioteca um arquivo de calibração com N notas idênticas de duração conhecida.

## Escopo em etapas

- **E1** Validação do hardware: buzzer emitindo notas por PWM; encoder e botões lidos.
- **E2** LVGL no OLED com menu navegável pelo encoder (lista fixa).
- **E3** Cartão montado e menu populado com os arquivos reais.
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

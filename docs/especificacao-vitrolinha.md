# Vitrolinha — tocador de músicas embarcado sobre Zephyr RTOS

**Especificação, versão 3** — kit de desenvolvimento ZBook (RP2350B), revisão P2

| | |
|---|---|
| Disciplina | Projetos de Sistemas Embarcados |
| Autor | Saulo |
| Plataforma | ZBook ([github.com/zephyr-book/zbook](https://github.com/zephyr-book/zbook)) — RP2350B, Arm Cortex-M33 duplo, 520 KB SRAM |
| Sistema operacional | Zephyr RTOS |
| Linguagem | C |
| Interface gráfica | LVGL sobre display OLED 128x64 monocromático |
| Saída de áudio | Buzzer piezoelétrico da própria placa, acionado por PWM |
| Revisão | Versão 3 — incorpora a verificação de hardware no Devicetree e a revisão de arquitetura dela decorrente |

---

## 1. Histórico da revisão

A proposta original era um leitor de livros digitais. O feedback recebido apontou
que o display de 128x64 pixels é impeditivo para leitura de texto longo, o que se
confirma: a tela acomoda cerca de 16 caracteres por linha e 8 linhas, tornando a
experiência inviável como aplicação real.

O escopo foi redirecionado para um tocador de músicas, preservando a parte da
proposta considerada válida: o menu de seleção navegável, a máquina de estados da
interface e a leitura de arquivos do cartão microSD. A tela pequena deixa de ser
um problema, porque a informação a exibir passa a ser curta.

**A versão 3 decorre da verificação do hardware.** A versão 2 listava três riscos
de hardware a confirmar, sendo um deles bloqueante. Os três foram fechados por
leitura do Devicetree da placa e do código de bring-up do fabricante, e o
resultado dessa verificação invalidou parte do desenho anterior. As mudanças de
fundo estão na seção 13.

---

## 2. Objetivo

Desenvolver um tocador de músicas sobre o kit ZBook que percorra os arquivos
musicais armazenados no cartão microSD, apresente-os em um menu navegável pelo
encoder rotativo e reproduza a faixa selecionada pelo buzzer da placa, exibindo o
progresso da reprodução no display OLED e aceitando os comandos de tocar, pausar,
avançar e retroceder pelos botões.

A reprodução usa o buzzer acionado por PWM, portanto o resultado sonoro é uma
melodia monofônica sintetizada, e não áudio gravado. Essa decisão é deliberada:
elimina dependência de hardware externo e mantém o mérito técnico do trabalho na
parte que interessa à disciplina, que é o escalonamento temporal da reprodução
concorrente com a interface gráfica. A seção 12 descreve o caminho de evolução
para áudio real.

---

## 3. Recursos da placa utilizados

Todos os pinos e nós abaixo foram lidos do Devicetree da revisão **P2** da placa.
A revisão importa: o encoder, o nó do display e a detecção de cartão existem
somente no P2, e o alvo de compilação precisa ser `zbook@p2/rp2350b/m33`.

| Recurso | Nó / alias | Pinos | Uso no projeto |
|---|---|---|---|
| Buzzer piezoelétrico | `pwm_buzzer` | GPIO 32, PWM slice 8 canal A | Síntese das notas; a frequência do PWM define a altura |
| Display OLED SSD1306 | `oled` | I²C0 @ 0x3c, SDA 4, SCL 5 | Interface em LVGL: menu de faixas e tela de reprodução |
| Encoder rotativo PEC12R | `encoder_qdec` | A GPIO 42, B GPIO 43 | Navegação no menu |
| Clique do encoder | `enc_button` | GPIO 38 | Seleção da faixa |
| Botões 0 a 3 | `button0`..`button3` | GPIO 23, 22, 19, 18 | Transporte: anterior, tocar/pausar, seguinte, alternar tela |
| Cartão microSD | `sdhc0` | SPI0: SCK 34, MOSI 35, MISO 36, CS 37 | Biblioteca de arquivos musicais |
| Detecção de cartão | `sd_cd` | GPIO 33 | Presença do cartão, ligada à pilha SD por `cd-gpios` |
| Potenciômetro | `potentiometer` | GPIO 41, ADC canal 1 | Ajuste de volume (escopo opcional) |
| LEDs 0 a 3 | `led0`..`led3` | GPIO 24 a 27 | Instrumentação de temporização; realimentação visual opcional |
| LEDs RGB | `ws2812` | GPIO 31, via PIO1 | Realimentação de estado (opcional) |
| Flash interna | — | — | Firmware e preferências persistentes via subsistema `settings` |

Nenhum componente adicional é necessário. O único insumo é um cartão microSD.

O buzzer é **passivo**, o que era o risco bloqueante da versão 2 (seção 11 e
apêndice A). O slice de PWM que o aciona é exclusivo dele, o que era o segundo
risco. O cartão está em um barramento SPI separado do sensor inercial, o que
responde parcialmente ao terceiro.

---

## 4. Formato dos arquivos musicais

O formato adotado é **RTTTL**, textual e compacto, que descreve melodias
monofônicas por uma sequência de notas com duração, altura e oitava, precedida de
um cabeçalho com duração padrão, oitava padrão e andamento em batidas por minuto.
A escolha se justifica por três motivos:

- É textual e legível, o que facilita depuração e criação de arquivos de teste.
- Os arquivos têm poucos quilobytes, o que permite carregá-los integralmente em
  um buffer de tamanho fixo e concentrar o esforço no escalonamento temporal.
- O interpretador precisa ser escrito do zero, incluindo o cálculo de frequência
  a partir da nota e da oitava, o que constitui trabalho técnico próprio e não
  uso de biblioteca pronta.

O cabeçalho RTTTL carrega o **nome da melodia**, e é dele que sai o texto exibido
na tela — não do nome do arquivo. Isso evita habilitar nomes longos no sistema de
arquivos FAT, que custaria memória estática, e ainda entrega um nome legível em
vez do formato 8.3 em maiúsculas que a leitura de diretório devolve.

Um segundo formato próprio, em texto separado por vírgulas com pares de
frequência e duração, é suportado para testes de bancada e para o **arquivo de
calibração**: N notas idênticas de duração conhecida, usado para medir o erro
temporal acumulado.

---

## 5. Requisitos

### 5.1 Requisitos funcionais

| ID | Descrição | Prioridade |
|---|---|---|
| RF01 | Listar os arquivos musicais presentes no cartão microSD em um menu na tela | Alta |
| RF02 | Navegar pelo menu e selecionar a faixa por meio do encoder rotativo | Alta |
| RF03 | Interpretar o arquivo selecionado e reproduzir a melodia pelo buzzer | Alta |
| RF04 | Exibir, durante a reprodução, o nome da faixa, o tempo decorrido e uma barra de progresso | Alta |
| RF05 | Alternar entre tocar e pausar, retomando do ponto em que parou | Alta |
| RF06 | Avançar para a faixa seguinte e retroceder para a anterior por botões | Alta |
| RF07 | Alternar entre a biblioteca e a tela de reprodução, nos dois sentidos, sem interromper o áudio | Média |
| RF08 | Ajustar o volume pelo potenciômetro, com efeito imediato (fora do MVP) | Baixa |
| RF09 | Avançar automaticamente para a próxima faixa ao fim da atual | Média |
| RF10 | Persistir a última faixa e o volume entre reinicializações | Baixa |
| RF11 | Sinalizar o estado da reprodução nos LEDs RGB | Baixa |

O RF07 é a versão corrigida do requisito da revisão anterior, que pedia apenas
"retornar ao menu". Com os quatro botões já atribuídos ao transporte e o encoder
à navegação, não sobrava controle para o caminho de volta: era possível sair da
tela de reprodução e não havia como voltar a ela. O requisito passa a ser
bidirecional e um único botão o atende.

### 5.2 Requisitos não funcionais

| ID | Descrição | Meta |
|---|---|---|
| RNF01 | Erro de tempo acumulado ao final de uma melodia de 60 s | menor que 1% da duração |
| RNF02 | Desvio de início de cada nota em relação ao instante previsto | menor que 5 ms |
| RNF03 | Latência entre o acionamento de um botão e o efeito audível, incluindo troca de faixa | menor que 100 ms |
| RNF04 | Atualização da interface sem interferir na temporização das notas | sem violação do RNF02 |
| RNF05 | Estabilidade do volume com o potenciômetro parado | sem oscilação perceptível; aplicável apenas se o RF08 for implementado |
| RNF06 | Uso de RAM independente do tamanho do arquivo | buffers de tamanho fixo |
| RNF07 | Comportamento após remoção do cartão durante a reprodução | sem travamento do firmware; a faixa em curso termina |

O RNF03 abrange explicitamente avançar e retroceder, que exigem abrir e
interpretar outro arquivo — é o caso mais caro, e não faria sentido medir apenas
o mais barato. O RNF07 promete **ausência de travamento**, não recuperação
automática do cartão: a seção 9 explica por que a segunda coisa não é sustentável
sobre o Zephyr atual.

---

## 6. Arquitetura de software

O firmware separa a reprodução da interface, de modo que nenhuma operação gráfica
ou de cartão possa atrasar a próxima nota. Essa fronteira é a decisão de projeto
central do trabalho.

A plataforma é de **núcleo único**. Embora o RP2350B tenha dois Cortex-M33, o
Zephyr não possui porte SMP para Cortex-M — a única implementação em `arch/arm/`
é para Cortex-A/R, e a documentação oficial do RP2350 registra que o suporte roda
em um núcleo só. A consequência é que a disciplina de prioridades e de contexto
não é redundância defensiva: é o único mecanismo de isolamento disponível.

### 6.1 Módulos

| Módulo | Responsabilidade |
|---|---|
| `player` | Máquina de estados da reprodução: parado, tocando, pausado. Arma o temporizador |
| `synth` | Tradução de nota e oitava em frequência e acionamento do PWM. Único módulo que conhece o buzzer |
| `parser` | Interpretação do RTTTL a partir do buffer em RAM, grade temporal, duração total, transposição |
| `storage` | Montagem do cartão, listagem da biblioteca, leitura do arquivo, presença do cartão |
| `ui` | Telas em LVGL, máquina de estados de navegação |
| `input` | Botões de transporte pelo subsistema `input`, convertidos em comandos de alto nível |
| `volume` | Leitura filtrada do ADC e conversão em nível discreto (opcional) |

### 6.2 Threads e contextos

| Contexto | Prioridade | Papel |
|---|---|---|
| Callback do `k_timer` | interrupção | **Dispara cada evento sonoro.** Ajusta o tom, retira o próximo evento da fila, reagenda e atualiza o instantâneo de estado |
| `player` | Alta | Máquina de estados. Acordado por comando do usuário, fila baixa ou fim de faixa. Nunca executa E/S de cartão, I²C ou gráficos |
| `input` | Média-alta | Traduz eventos de encoder e botões em comandos e os publica em fila |
| `ui` | Baixa | Executa o laço do LVGL em intervalo fixo e redesenha apenas as regiões alteradas |
| `loader` | Baixa | Lê o arquivo para o buffer, interpreta e preenche a fila de eventos |

O disparo da nota ocorre em **contexto de interrupção**, e não em thread. Isso é
possível porque o driver de PWM do RP2350 no Zephyr não usa mutex, semáforo, log
nem espera bloqueante no caminho de ajuste — são escritas em registrador. O ganho
não é apenas de jitter, embora ele exista: a latência de interrupção é da ordem
de 1 a 3 µs, contra dezenas de µs para despertar uma thread. O ganho principal é
que **o instante da nota deixa de depender do escalonador**, o que é uma
afirmação verificável e mais forte do que a ausência de inversão de prioridade.

A comunicação entre contextos usa filas de mensagens. A única exceção é o
instantâneo de estado descrito em 6.5.

### 6.3 Carregamento do arquivo

O arquivo é lido **integralmente para um buffer estático de 4 KB** antes de a
reprodução começar. Um RTTTL típico ocupa de 100 a 400 bytes, e 4 KB comportam
cerca de mil notas. Arquivos maiores são rejeitados com mensagem, e não truncados.

A consequência importante é que **a reprodução não acessa o cartão nenhuma vez**.
Isso satisfaz o RNF06 por construção, torna o RNF07 quase gratuito, e faz do
reinício da faixa uma reinterpretação a partir do byte zero.

O desenho alternativo, em que o `loader` lê do cartão durante a reprodução para
alimentar a fila, foi descartado: pagaria a complexidade de tratar subalimentação
de fila e ainda assim ficaria exposto à latência do cartão.

### 6.4 Temporização e progresso

**A grade.** Calcular a duração de cada nota em milissegundos por divisão inteira
e somar as durações acumula cerca de meio milissegundo de arredondamento por
nota; em trezentas notas, até 150 ms. Isso passaria no RNF01, mas por margem
acidental, e não por projeto.

O `parser` mantém em vez disso um contador inteiro de subdivisões, `n64`, igual
ao número de sexagésimas-quartas desde o início da faixa, e converte cada
instante com uma única operação em 64 bits:

```c
offset_us = (uint64_t)n64 * 60000000ULL * 4 / (bpm * 64);
```

O erro deixa de ser acumulativo e passa a ser um truncamento único por evento,
limitado a um microssegundo. **O RNF01 fica satisfeito por construção.**

A base é a sexagésima-quarta, e não a trigésima-segunda, porque uma semifusa
pontuada vale uma vírgula cinco trigésimas-segundas, que não é inteiro. Em
sexagésimas-quartas toda duração do RTTTL, pontuada inclusive, cai em número
inteiro.

**A fila.** O que atravessa a fila são eventos, não notas:

```c
struct evt { uint32_t offset_us; uint32_t freq_hz; };  /* freq_hz == 0 => silêncio */
```

Cada nota gera dois eventos, o tom e o silêncio de articulação que o segue. O
callback do temporizador fica uniforme, sem distinguir ligar de desligar. A fila
tem 32 posições, equivalentes a 16 notas, ocupando 256 bytes.

**O agendamento.** O callback reagenda com timeout absoluto do kernel, calculando
`base_us + offset_us`. Nunca dorme por duração. Um detalhe do Zephyr favorece
esse desenho: dentro de um callback de timeout, a leitura do tempo devolve o
instante *agendado*, e não o real, precisamente para que o próximo prazo seja
derivado de onde o evento deveria ter ocorrido.

A frequência de tique do sistema é elevada para 20 kHz, dando 50 µs de
quantização contra a meta de 5 ms — folga de duas ordens de grandeza. Sob kernel
tickless isso não implica interrupções periódicas adicionais.

**A articulação.** Notas iguais consecutivas se fundiriam em um tom só sem
separação. Cada nota soa por sua duração menos o menor valor entre um décimo dela
e 20 ms, e silencia o restante da fatia. O silêncio fica dentro da fatia, sem
perturbar a grade.

**A pausa.** A base temporal não permanece fixa através de uma pausa: ela desliza.
Ao retomar, a base é somada ao intervalo decorrido desde o início da nota
corrente, que recomeça inteira. Uma única variável corrige simultaneamente o
agendamento e o tempo decorrido exibido.

**O progresso.** A duração total é o deslocamento do último evento, obtido na
mesma passagem que preenche a fila. A barra é o quociente entre tempo decorrido e
duração total, calculado pela thread de interface.

### 6.5 Propriedade do estado

A interface precisa do tempo decorrido sem interrogar o player a cada quadro. A
solução é um **registro instantâneo de escritor único** — estado, base temporal,
duração total e índice da faixa — escrito apenas pelo lado do player e lido sem
bloqueio pela interface, que calcula o decorrido por subtração.

Isso é uma exceção deliberada à comunicação por filas, e vale registrar por quê:
a regra útil não é "nenhum estado compartilhado", que seria incompatível com o
próprio requisito da barra de progresso, mas **nenhum estado mutável compartilhado
sem disciplina de propriedade**. Um escritor, leitura sem bloqueio, campos
alinhados. Todo o resto é fila.

---

## 7. Interface gráfica em LVGL

O LVGL é configurado com profundidade de cor de 1 bit. O buffer de renderização é
de **quadro inteiro**: 128x64 a 1 bpp são 1024 bytes, e cerca de 2 KB no total com
o buffer de conversão monocromática, o que torna a renderização parcial uma
economia sem propósito e evita as complicações de área parcial. O LVGL continua
invalidando e transferindo apenas as regiões alteradas.

A biblioteca oferece suporte nativo a encoder rotativo como dispositivo de
entrada, com grupos de foco, o que atende diretamente à navegação pedida no menu.
O vínculo entre o subsistema `input` do Zephyr e o dispositivo de entrada do LVGL
é declarativo, no Devicetree. A criação do grupo de foco e sua associação ao
dispositivo, porém, são da aplicação.

| Tela | Conteúdo |
|---|---|
| Biblioteca | Lista rolável de faixas, com destaque no item em foco, marcador na faixa em reprodução e marcador de erro em arquivos rejeitados |
| Reprodução | Nome da faixa, tempo decorrido e total, barra de progresso e indicador de estado |
| Volume | Sobreposição temporária exibida ao girar o potenciômetro (opcional) |
| Erro | Mensagem em caso de cartão ausente, cartão removido ou arquivo inválido |

A taxa de redesenho é limitada deliberadamente, na ordem de dez quadros por
segundo. Há um motivo concreto além da economia: o barramento I²C da placa está
declarado a 100 kHz, e um quadro cheio custaria cerca de 92 ms de transferência.
A aplicação eleva o barramento a 400 kHz, o que reduz esse custo para cerca de
23 ms, e o redesenho por região mantém o caso comum em algumas centenas de bytes.

Nada disso ameaça a temporização do áudio: a interface é de baixa prioridade e
preemptível, o player não toca no I²C, e o disparo da nota está em contexto de
interrupção. O que a taxa de quadros afeta é a fluidez da transição de tela.

---

## 8. Controles

| Controle | Ação |
|---|---|
| Girar o encoder | Move o foco na biblioteca |
| Clicar o encoder | Seleciona a faixa e a toca imediatamente, substituindo a atual |
| Botão físico 1 | Faixa anterior |
| Botão físico 2 | Tocar / pausar |
| Botão físico 3 | Faixa seguinte |
| Botão físico 4 | Alterna entre biblioteca e reprodução |

A regra que organiza o conjunto é: **o transporte é global e independente da tela
exibida; o encoder é sempre navegação**. Não há fila de reprodução — selecionar
troca a faixa em curso.

Ao fim de uma faixa o player para e permanece na tela de reprodução, com o
progresso completo; o botão de tocar reinicia do começo. Esse comportamento
reduz o RF09, avanço automático, a uma substituição de uma linha.

Os botões da revisão P2 não têm serigrafia, e sua ordem física não corresponde à
ordem numérica dos aliases. O mapeamento acima é por posição física, e a
identificação é tarefa da etapa E1.

---

## 9. Tratamento de falhas

| Falha | Comportamento |
|---|---|
| Sem cartão no início | Tela de erro. A presença é detectada pela pilha SD através do pino dedicado; na inserção, tenta montar e varrer a biblioteca |
| Cartão removido durante a reprodução | **A faixa em curso termina normalmente**, porque o arquivo já está integralmente em RAM e nenhum contexto de reprodução acessa o sistema de arquivos. A biblioteca é marcada inválida, avançar e retroceder são desabilitados, e a tela de erro aparece ao retornar ao menu |
| Arquivo inválido | O interpretador rejeita, a tela de erro exibe o motivo por cerca de dois segundos, retorna-se à biblioteca e o item fica marcado |

**A remontagem a quente do cartão não é prometida.** A questão está registrada no
Zephyr como não suportada e foi encerrada sem previsão de implementação: a
montagem falha na reinserção, com atrasos de mais de dez segundos relatados nas
tentativas de desmontagem. A aplicação tenta a remontagem, porque custa pouco e
por vezes funciona, mas o comportamento documentado em caso de falha é a
solicitação de reinicialização. O RNF07 é redigido de acordo.

Os tempos limite padrão do subsistema SD são de dez segundos com três tentativas,
o que permitiria a uma única leitura bloquear por dezenas de segundos. São
reduzidos a 500 ms e uma tentativa.

---

## 10. Limitações reconhecidas

| Limitação | Consequência e tratamento |
|---|---|
| Áudio monofônico | O buzzer produz uma única frequência por vez, sem acordes nem timbre. Resultado comparável ao de um toque de telefone antigo. Aceito como característica do escopo |
| Resposta desigual do transdutor | Um piezo tem resposta fraca nas frequências graves. Notas abaixo da região útil saem audivelmente mais fracas, deixando a melodia com volume irregular. Tratado por transposição da faixa inteira em oitavas completas, preservando os intervalos; o limiar é obtido por medição, não estimado |
| Controle de volume grosseiro | O volume varia com o ciclo de trabalho de forma não linear e com faixa útil estreita, entregando pouco resultado perceptível para esforço de calibração considerável. Deslocado para fora do escopo mínimo; se implementado, usará poucos níveis discretos |
| Núcleo único | Não há SMP para Cortex-M no Zephyr, portanto o segundo núcleo do RP2350B não está disponível ao escalonador. O isolamento temporal depende inteiramente de contexto e prioridade |
| Remontagem do cartão | Não suportada de forma confiável pela pilha SD do Zephyr. Ver seção 9 |
| Biblioteca limitada a 32 faixas | Consequência direta do requisito de RAM fixa. Arquivos além do limite são ignorados com aviso |

---

## 11. Plano de entregas

| Etapa | Entrega | Critério de conclusão |
|---|---|---|
| E0 | Ambiente | Workspace `west` com a placa fora da árvore, repositório iniciado, compilação para `zbook@p2/rp2350b/m33` acendendo um LED |
| E1 | Validação do hardware | Buzzer emitindo notas de frequência controlada; varredura de frequência determinando o limiar de resposta do transdutor; verificação de estalo na troca de nota; encoder e botões lidos, com a ordem física dos botões identificada; medição do custo de abertura e leitura de arquivo |
| E2 | Interface base | LVGL operando no OLED com menu navegável pelo encoder, ainda com lista fixa |
| E3 | Biblioteca no cartão | Cartão montado e menu populado a partir dos arquivos reais, com os nomes lidos do cabeçalho RTTTL |
| E4 | Reprodução | Faixa selecionada tocando corretamente, com tela de progresso atualizando |
| E5 | Controles | Tocar, pausar, avançar e retroceder funcionando durante a reprodução |
| E6 | Medição e documentação | Métricas dos requisitos não funcionais coletadas, relatório e demonstração |
| E7 | Extensões opcionais | Volume pelo potenciômetro, persistência de preferências, avanço automático e sinalização por LEDs |

As etapas E0 a E5 constituem o produto mínimo viável. A E1 ganhou peso em relação
à versão anterior: ela agora produz três números que alimentam decisões de
projeto — o limiar de transposição, o custo de leitura do cartão para o orçamento
do RNF03, e a confirmação de que a troca de nota não produz artefato audível.

---

## 12. Caminho de evolução para áudio real

A arquitetura foi desenhada para que a troca da saída de áudio não exija
reescrever a aplicação: o módulo de síntese é a única fronteira que conhece o
buzzer, e o restante opera sobre a abstração de uma fonte sonora com posição
temporal. Duas evoluções são possíveis:

- **Áudio PWM com filtro passivo e amplificador**: permite reproduzir arquivos
  WAV do cartão por modulação do ciclo de trabalho, alimentada por DMA em buffer
  duplo. Custo de hardware baixo, qualidade modesta.
- **Conversor externo por I2S**: qualidade adequada, três pinos de sinal.
  Depende de duas confirmações, nenhuma delas disponível hoje: que haja suporte a
  I2S no port do RP2350 no Zephyr, e que o conector de expansão da revisão P2
  exponha pinos livres. O conector não está descrito no repositório da placa e o
  esquemático da revisão não está publicado.

Nenhuma das duas está no escopo atual. São registradas para demonstrar que a
decisão pelo buzzer é um recorte de escopo consciente, não um limite
arquitetural.

---

## 13. O que mudou da versão 2 para a versão 3

| Mudança | Motivo |
|---|---|
| Riscos de hardware fechados | Verificação no Devicetree e no código de bring-up. O buzzer é passivo, o slice de PWM é exclusivo, o cartão está em barramento próprio |
| Segundo núcleo retirado de consideração | Não existe porte SMP para Cortex-M no Zephyr. A versão 2 não mencionava o assunto; omitir seria pior que decidir |
| Streaming substituído por carga integral em RAM | Um arquivo de 400 bytes não justifica leitura durante a reprodução. A troca elimina o risco em vez de administrá-lo |
| Grade temporal em subdivisões inteiras | A soma de durações arredondadas acumulava erro. A correção torna o RNF01 estrutural |
| Disparo da nota movido para contexto de interrupção | O driver de PWM permite, e o instante deixa de depender do escalonador |
| RF07 tornado bidirecional | O requisito anterior descrevia um caminho sem volta |
| Buffer parcial trocado por quadro inteiro | A 1 bpp o quadro tem 1 KB; o buffer parcial economizava nada e complicava |
| RNF07 rebaixado de recuperação para ausência de travamento | A remontagem a quente não é suportada pelo Zephyr, e prometer o que a plataforma não entrega seria erro de especificação |
| Nome exibido lido do cabeçalho RTTTL | Evita habilitar nomes longos no FAT e entrega texto legível |
| Etapa E0 acrescentada | A versão 2 começava supondo o ambiente pronto |

---

## 14. Critérios de avaliação propostos

- Demonstração completa: seleção de faixa no menu, reprodução, pausa, retomada e
  troca de faixa.
- Tabela de medições dos requisitos não funcionais, com o método descrito,
  incluindo verificação da temporização por instrumentação em pino de depuração.
  A medição usa contador de ciclos de 64 bits: a versão de 32 bits dá a volta a
  cada 28,6 s a 150 MHz e inviabilizaria uma corrida de 60 s.
- Análise do consumo de memória por thread e justificativa do dimensionamento das
  pilhas, coletada pelo analisador de threads do Zephyr.
- Documentação da máquina de estados da reprodução e da interface.
- Comportamento sob falha induzida: cartão ausente, cartão removido durante a
  reprodução, arquivo inválido e troca de faixa durante a reprodução.
- Rastreabilidade das decisões de projeto às verificações que as sustentam
  (apêndice A).

---

## Apêndice A — Verificação de hardware

Os três riscos da versão 2 foram fechados por leitura direta das fontes abaixo.
Nenhum depende de medição em bancada.

| Risco (v2) | Resultado | Fonte |
|---|---|---|
| O buzzer é passivo ou ativo? **Bloqueante** | **Passivo, piezoelétrico.** GPIO 32, PWM slice 8 canal A, acionado por MOSFET de lado baixo (Q7) | `zbook_rp2350b_m33_p2-common.dtsi`, comentário `BUZZER_GPIO (piezo, Q7)` e nó `pwm_buzzer`; código de bring-up do fabricante que varia o período para gerar 440, 880, 1760 e 3520 Hz |
| O PWM do buzzer compartilha recursos? | **Não.** O slice 8 é exclusivo. Os LEDs 0 a 3 são GPIO puro, os LEDs RGB usam PIO, e o único outro consumidor de PWM é o motor, no slice 3 | Mesmo arquivo, bloco `&pwm` com `divider-int-3` e `divider-int-8` |
| O microSD compartilha barramento com o conector de expansão? | **Parcialmente respondido.** O cartão está no SPI0 e o sensor inercial no SPI1. Se o conector da revisão P2 toca o SPI0 é indeterminável: o nó do conector foi removido e o esquemático não está publicado | Mesmo arquivo, nó `sdhc0` e remoção de `pico_header` |

Riscos abertos que dependem de bancada ou de documento não publicado:

1. Frequência de ressonância do transdutor, que define o limiar de transposição.
   Nenhum número de peça consta no repositório. **Medição na E1.**
2. Se a reprogramação do período no meio de um ciclo produz artefato audível na
   troca de nota. A ausência de reinício do contador está confirmada no código do
   driver; a consequência sonora, não. **Bancada na E1.**
3. Se há resistores de elevação externos nas linhas dos botões e do encoder. Os
   nós não declaram elevação interna, e outro sensor no mesmo arquivo declara,
   com comentário explicando que ali não há externo — o que sugere que nos demais
   há. **Bancada na E1.**
4. Quais pinos o conector de expansão da revisão P2 expõe. Obter o esquemático da
   revisão junto ao fornecedor fecharia 1, 3 e 4 de uma vez.

## Apêndice B — Configuração de compilação

Alvo obrigatório:

```
west build -b zbook@p2/rp2350b/m33
```

O sufixo de revisão é obrigatório. Embora o descritor da placa declare a revisão
P2 como padrão, esse campo é ignorado para placas de formato personalizado, e o
script de revisão recai em P1 quando nada é especificado. Compilar sem o sufixo
produz um binário para uma placa sem encoder e sem display, sem emitir erro.

As configurações e sobreposições de Devicetree exigidas estão registradas em
`CLAUDE.md`, na raiz do repositório, junto das justificativas de cada uma. Três
merecem destaque por serem silenciosamente destrutivas se omitidas:

- A prioridade da fila de trabalho do LVGL tem padrão zero, a mais alta
  preemptível, o que a colocaria acima do player e inverteria a arquitetura sem
  gerar erro.
- O buffer de conversão monocromática é obrigatório; sem ele o caminho de
  display monocromático não é registrado e a inicialização falha.
- O período do PWM deve ser derivado da taxa reportada pelo driver, nunca de
  constante. Com o divisor fixo declarado pela placa, o driver não valida o
  limite do registrador de 16 bits, e um cálculo baseado no relógio do sistema
  produz uma nota errada sem qualquer aviso.

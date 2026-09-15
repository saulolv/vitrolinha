# RNF03: quanto custa abrir e ler uma faixa

**Issue:** [#8](https://github.com/saulolv/vitrolinha/issues/8) ·
**Bancada:** `bench/rnf03/`, rodada com `scripts/bench.sh` ·
**Data:** 2026-09-15 · **Alvo medido:** `native_sim/native/64`

O RNF03 dá 100 ms entre o botão e o efeito audível, **incluindo seguinte e
anterior** — que exigem abrir e interpretar outro arquivo. Ler 400 bytes de um
cartão a 24 MHz é trivial. O que não é óbvio é o `fs_open`: o FatFs varre o
diretório à procura da entrada, e ninguém sabe de cabeça quantos setores isso
custa.

**Resultado:** a pior faixa possível — 4096 B, o teto do buffer da aplicação —
custa **10 setores em 4 chamadas de leitura**, o que dá **11,8 ms** com o
tempo de acesso típico de um cartão SDHC. O orçamento do armazenamento é de
60 ms. **Não é preciso pré-carregar as faixas vizinhas**, e a decisão está
registrada na [ADR 0006](../adr/0006-sem-pre-carga-de-faixas-vizinhas.md).

## O que foi medido e o que foi modelado

A separação importa mais do que o número, e vale a pena ser chato com ela.

**Medido, e transferível para a placa:** quantos setores o FatFs pede, em
quantas chamadas, e de que tamanho cada uma. Isso é software puro acima do
`disk_access` — não depende de transporte, de processador nem de velocidade de
barramento, só da geometria do volume. É por isso que o cartão de mentira
reproduz a geometria de um cartão de verdade (adiante).

**Modelado:** o tempo. O `native_sim` não pode cronometrar isto, e não por
imprecisão: ele trata a execução de código como instantânea, e o relógio
simulado só anda quando a CPU é parada de propósito. A própria bancada
demonstra isso na seção `[1]` do relatório — dez milhões de somas em ponto fixo
levam **0 µs** de tempo simulado. Um zero desses, tomado por medição, seria pior
do que não medir.

> *"Conceptually it could be thought as if the MCU was running at an infinitely
> high clock, and therefore no simulated time passes while executing
> instructions."*
> — `zephyr/boards/native/native_sim/cpu_wait.c`

O que a bancada faz, então, é cobrar do relógio simulado — com `arch_busy_wait`,
o único jeito de fazer tempo passar ali — o custo que cada acesso teria no SPI
da placa. Esse custo vem de `bench/rnf03/sd_cost.h`, e é onde mora a única
incerteza de verdade.

## A régua: quanto custa um setor

Os bytes não são estimativa. Saem da leitura do caminho que o Zephyr percorre a
cada `disk_access_read`, com arquivo e linha:

| Trecho | Bytes | Onde |
|---|---|---|
| CMD17/CMD18, com dados a seguir | 9 | `sdhc_spi.c:392`, `SD_SPI_CMD_SIZE + 3` |
| Espera do token 0xFE | **tempo do cartão** | `sdhc_spi.c:431`, um byte por sondagem |
| Bloco de dados | 512 | |
| CRC16 mais byte de encerramento | 3 | `sdhc_spi.c:611` |
| Sondagem de ocupação | 1 | `sdhc_spi.c:180`, em `sdmmc_wait_ready` |
| **CMD13 depois de toda leitura** | 21 | `sd_ops.c:534` e `:20` |
| CMD12, só quando houve CMD18 | 21 | `sdhc_spi.c:668` |

O CMD13 é o achado que ninguém esperaria: `card_read` **não termina no dado**.
Ele chama `sdmmc_wait_ready`, que sonda a ocupação e manda um SEND_STATUS
completo antes de devolver o controle. São 22 bytes a mais por chamada de
leitura, sem transportar dado nenhum.

Somando, a 24 MHz:

- **1 setor** (CMD17): 546 B = **182 µs** de barramento
- **8 setores** (CMD18): 4172 B = **1390 µs** de barramento

Mais o tempo de acesso do cartão, **uma vez por bloco**. Esse é o parâmetro. O
padrão da bancada é 1 ms, que é o TAAC que a especificação física do SD **fixa**
para CSD versão 2.0 — isto é, para todo cartão SDHC. Não é média de medição.
Por isso o relatório não depende desse valor: a seção `[6]` varre uma faixa
inteira e diz onde a decisão mudaria.

## A bancada

Um disco registrado pela própria aplicação (`bench/rnf03/sim_card.c`), com a
geometria de um cartão de verdade:

| | |
|---|---|
| Capacidade anunciada | 8 GiB |
| Formato | FAT32, duas FATs, com tabela de partição |
| Aglomerado | 32 KiB, que é o que o formatador oficial do SD usa de 4 a 32 GB |
| Nomes longos | **desligados**, como no firmware |
| Biblioteca | 32 faixas de 128 B a 4096 B, nomes 8.3 em maiúsculas |

A geometria é o ponto, e é por isso que o disco é **esparso**: só os setores
efetivamente escritos ocupam memória, e formatar 8 GiB toca cerca de 4200
setores — as duas FATs, a área reservada e o aglomerado da raiz. Um disco de RAM
comum, pequeno o bastante para caber, sairia FAT12 ou FAT16 com aglomerados de
512 B, e produziria uma contagem de setores que não valeria para o cartão.

Os nomes longos estarem desligados também não é detalhe: com LFN cada entrada de
diretório ocupa várias entradas de 32 bytes, o diretório cresce em setores, e a
varredura que o `fs_open` faz fica mais cara. Medir com LFN ligado mediria outra
coisa.

## O orçamento

O RNF03 são 100 ms de ponta a ponta. O armazenamento é só um dos trechos:

| Trecho | Custo |
|---|---|
| Antirrebote do botão | 20 ms, fixados por `debounce-interval-ms = <20>` |
| Fila de comandos | desprezível: `k_msgq` sem bloqueio |
| **Abrir e ler o arquivo** | **é o que esta bancada mede** |
| Interpretar e soar a primeira nota | 20 ms de reserva |

A reserva do interpretador é **premissa declarada, não medição** — o
interpretador não existe (issue #10). Está escrita em `bench/rnf03/bench.h` para
que revisá-la seja mexer numa linha. Sobram **60 ms** para o armazenamento.

## Resultados

### Carga de faixa — o caminho do RNF03

| Faixa | Bytes | Chamadas | Setores | µs modelados |
|---|---|---|---|---|
| `TRK00.TXT` | 128 | 2 | 2 | 2 364 |
| `TRK07.TXT` | 1024 | 2 | 3 | 3 542 |
| `TRK15.TXT` | 2048 | 2 | 5 | 5 886 |
| `TRK23.TXT` | 3072 | 3 | 8 | 9 411 |
| `TRK30.TXT` | 3968 | 4 | 10 | **11 765** |
| `TRK31.TXT` | 4096 | 3 | 10 | 11 755 |

A forma das chamadas explica os números melhor do que o total de setores. A
faixa de 4096 B lê o diretório em duas chamadas de um setor e depois **os oito
setores de dados de uma vez só**, porque o arquivo cabe inteiro dentro de um
aglomerado de 32 KiB e o FatFs emite um `disk_read` contíguo. Já a de 3968 B
paga uma chamada a mais: sete setores cheios mais um pedaço de 384 B, que vai
pelo buffer do arquivo. Por isso o pior caso é uma faixa **quase** do tamanho
máximo, e não a do tamanho máximo.

Somar setores apagaria essa diferença — uma chamada de oito setores usa CMD18 e
paga um CMD12; oito chamadas de um setor usam CMD17 e pagam oito CMD13. A
bancada guarda a forma das chamadas, não só o total, e é sobre ela que a
sensibilidade é recalculada.

### Sensibilidade ao tempo de acesso do cartão

| Acesso por bloco | Pior carga | Veredito |
|---|---|---|
| 0 µs | 1 765 µs | cabe |
| 250 µs | 4 265 µs | cabe |
| 500 µs | 6 765 µs | cabe |
| **1 000 µs** (TAAC do SDHC) | **11 765 µs** | **cabe** |
| 2 500 µs | 26 765 µs | cabe |
| 5 000 µs | 51 765 µs | cabe |
| 10 000 µs | 101 765 µs | ESTOURA |

**O orçamento só quebra se o cartão levar mais de 5 823 µs por bloco** — quase
seis vezes o TAAC que a especificação fixa para todo SDHC. É essa distância, e
não o número de 11,8 ms, que sustenta a decisão: ela sobrevive a um cartão
bastante pior do que o esperado.

### Achados de lado

**A varredura da biblioteca custa 96 setores, ~113 ms.** São as 32 faixas com
`readdir`, `open` e a leitura do cabeçalho de cada uma — porque o nome exibido
vem do cabeçalho RTTTL, não do nome 8.3. Está **fora** do caminho do RNF03:
acontece uma vez, na montagem. Mas define o tempo entre "cartão entrou" e "menu
pronto", que com a montagem dá ~117 ms, e isso merece uma tela de espera em vez
de uma tela vazia.

**A janela de setor do FatFs não ajuda em nada.** A bancada mediu a pior faixa
logo depois de uma remontagem, com a janela fria, e deu exatamente o mesmo: 4
chamadas, 10 setores. O motivo é que `f_open` chama `dir_find`, que **sempre
recomeça da entrada zero** do diretório. Guardar o último setor lido não adianta
quando a busca reinicia. Consequência prática: abrir a 32ª faixa custa um setor
de diretório a mais do que abrir a primeira, e nada que a aplicação faça muda
isso.

**`fs_mount` custa 3 setores.** Tabela de partição, setor de inicialização e
FSInfo. Nada a otimizar.

## O que esta medição não alcança

Uma coisa, e é séria. Se a resposta R1 do cartão não chegar dentro dos nove
bytes do pacote de comando, o `sdhc_spi` cai num laço de sondagem com
`k_msleep(10)` — **10 ms por comando** (`sdhc_spi.c:253`). São dois comandos por
chamada de leitura, e a pior faixa faz quatro chamadas. Um único atraso desses
custa 10 ms de uma vez; uma sequência deles estoura o orçamento sozinha,
independentemente de tudo que está acima.

Isso é comportamento de cartão, não de software, e nenhum modelo alcança. A
mesma bancada compila para a placa (`scripts/bench.sh --board`), onde o disco de
mentira some, o `k_cycle_get_64()` mede tempo de verdade e as colunas de setor
saem vazias. Rodá-la é item da E1.

Se aparecer, a saída **não** é pré-carga: é aumentar a folga de bytes do pacote
de comando no driver, que é onde está a causa. Pré-carga trataria o sintoma
pagando 12 KB de RAM.

## Reprodução

```
scripts/bench.sh              # simulador: contagem de setores + modelo
scripts/bench.sh --board      # placa: medição de verdade, para a E1
```

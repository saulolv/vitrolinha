# Vitrolinha

Tocador de músicas embarcado sobre **Zephyr RTOS** para o kit **ZBook**
(RP2350B, Arm Cortex-M33, 520 KB SRAM).

Lista os arquivos musicais de um cartão microSD em um menu no OLED 128x64,
navegável pelo encoder rotativo, e reproduz a faixa selecionada pelo buzzer
piezoelétrico da placa via PWM, com nome, tempo decorrido e barra de progresso na
tela. O áudio é monofônico e sintetizado — o mérito técnico está no escalonamento
temporal da reprodução concorrente com a interface gráfica, não na fidelidade.

Trabalho da disciplina de Projetos de Sistemas Embarcados.

## Compilar

O caminho padrão é por contêiner: fixa as versões do Zephyr e do SDK para todo
mundo e não exige instalar nada além do Docker. Ver
[ADR 0004](docs/adr/0004-ambiente-de-compilacao-em-docker.md).

```bash
scripts/setup.sh     # imagem + workspace west. Demora na primeira vez
scripts/build.sh     # firmware para a placa -> build-out/zephyr.uf2
scripts/sim.sh       # firmware no simulador, sem placa
scripts/test.sh      # testes unitários + cobertura
```

No Windows, rode pelo **Git Bash**. O `scripts/setup.sh` é idempotente: rodar de
novo só atualiza o que mudou.

Para gravar, conecte a placa pela USB com o BOOTSEL pressionado e copie
`build-out/zephyr.uf2` para o disco que aparecer.

### Sem contêiner

Se você já tem um workspace Zephyr montado, o repositório é o próprio manifesto
west (topologia T2):

```bash
west init -m https://github.com/saulolv/vitrolinha.git vitrolinha-ws
cd vitrolinha-ws
west update
west build -b zbook@p2/rp2350b/m33 vitrolinha
```

**O sufixo `@p2` é obrigatório.** Sem ele o build recairia na revisão P1 da
placa, que não tem encoder nem display. O projeto recusa esse alvo em três
pontos independentes — a string do alvo no `CMakeLists.txt`, a `BOARD_REVISION`
já interpretada, e os `BUILD_ASSERT` de `src/board_zbook.h` sobre o
devicetree — porque o Zephyr sozinho não recusaria, e o binário errado sairia
sem aviso.

## Sem a placa: o alvo de simulação

O projeto compila para dois alvos, e só para estes dois:

| Alvo | Para quê |
|---|---|
| `zbook@p2/rp2350b/m33` | a placa |
| `native_sim/native/64` | o simulador |

O simulador reproduz o OLED numa janela, com o **mesmo formato do
SSD1306** — mono de 1 bit, tiling vertical — e os mesmos ajustes de LVGL do
firmware de produção. Os botões e o clique do encoder passam pelos drivers
reais `gpio-keys` e pelo subsistema `input`; o que muda é a origem do sinal,
que é o teclado do host em vez de um pino.

| Tecla | Efeito |
|---|---|
| `1` `2` `3` `4` | botões de transporte |
| `Enter` | clique do encoder |
| setas ← → | girar o encoder |

A rotação é o único ponto em que o caminho difere: o `gpio-qdec` espera
quadratura A/B limpa, que duas teclas não produzem, então `src/sim_encoder.c`
converte as setas direto em `INPUT_REL_WHEEL` — o mesmo evento que o driver
emitiria. É código testado como qualquer outro.

**O que o simulador não faz:** som. Não existe PWM emulado no Zephyr, e ouvir
a melodia continua exigindo a placa. Ele também não substitui a bancada em
nada da E1 — limiar do piezo, estalo na troca de nota, ordem física dos
botões e jitter real.

**A janela** precisa de um servidor gráfico, que o contêiner não tem. Rodando
por `scripts/sim.sh`, o firmware executa e o relatório sai no terminal, mas a
tela não aparece. Para vê-la no Windows, rode o binário pelo WSL, que tem
WSLg — o cabeçalho de `scripts/sim.sh` traz os dois comandos. No Linux, basta
executar `build-out/zephyr-sim.elf`.

### Comandos avulsos no contêiner

```bash
scripts/zephyr.sh west boards | grep zbook
scripts/zephyr.sh bash            # sessão interativa no workspace
```

## Testes

Os módulos rodam em `native_sim` sob `ztest`, compilados a partir da mesma lista
de fontes que vai para a placa (`src/CMakeLists.txt`).

```bash
scripts/test.sh                # com cobertura e portão
scripts/test.sh --no-coverage  # só os testes
```

A meta é **100% de linhas e de ramos** nos módulos. `scripts/coverage_gate.py`
reprova a execução se houver lacuna. Ramos que o ambiente de teste
comprovadamente não consegue exercitar ficam em
[`tests/coverage-exclusions.json`](tests/coverage-exclusions.json), cada um com a
justificativa — e o portão também reprova exclusão que deixou de ser necessária,
para que a lista não vire depósito.

Relatório navegável em `build-out/coverage/index.html`.

### Nada escapa da medição

Cobertura de 100% só quer dizer alguma coisa se todo o código que deveria ser
medido chegou à medição. Um arquivo que fica fora da lista de fontes
simplesmente não aparece no relatório, e a porcentagem sobre o que sobrou
continua dizendo 100% — o número fica bonito enquanto a cobertura real cai.

Por isso o portão exige que **todo `.c` de `src/` apareça no relatório**. A
única isenção hoje é o `src/main.c`, declarada em `UNMEASURED_SOURCES` com a
razão (compilá-lo nos testes traria um segundo `main()`, que colidiria com o do
ztest). Arquivo novo que escape entra reprovando, e isenção que deixou de ser
necessária também.

Não há regra de qual diretório usar: `src/` é o layout convencional do Zephyr e
é onde tudo mora. O que o `src/CMakeLists.txt` lista, os testes compilam; o
`main.c` é acrescentado à parte, pelo `CMakeLists.txt` da raiz.

## Organização do repositório

| Caminho | O que é |
|---|---|
| `west.yml` | Manifesto west. Zephyr fixado em v4.4.2, placa fora da árvore |
| `prj.conf`, `app.overlay` | Configuração e devicetree comuns aos dois alvos |
| `boards/` | O que é específico de cada alvo: `.conf` e `.overlay` por alvo |
| `include/vitrolinha/` | Contratos compartilhados pelas três trilhas |
| `src/` | Implementação dos módulos e ponto de entrada. `src/CMakeLists.txt` é a única lista de fontes do projeto |
| `tests/` | Um diretório por módulo, em `ztest` sobre `native_sim` |
| `scripts/` | Bancada: imagem, workspace, build da placa, simulador, testes, portão de cobertura |
| `docker/` | Imagem enxuta de compilação |

## Documentos

| Documento | Para quê |
|---|---|
| [`docs/especificacao-vitrolinha.md`](docs/especificacao-vitrolinha.md) | Especificação v3: requisitos, arquitetura, métricas, plano de entregas |
| [`CLAUDE.md`](CLAUDE.md) | Plano de trabalho: pinos verificados, Kconfig, overlay, armadilhas, restrições de código |
| [`CONTEXT.md`](CONTEXT.md) | Glossário do domínio. O mesmo termo com o mesmo sentido em código, issues e relatório |
| [`docs/adr/`](docs/adr/) | Decisões de arquitetura, com as alternativas rejeitadas |

Leia o `CLAUDE.md` antes de escrever a primeira linha. Ele lista as
configurações que, se omitidas, quebram o projeto **em silêncio**.

## Como o trabalho está dividido

Três trilhas paralelas, costuradas por quatro contratos congelados antes da
divisão. Com os contratos no lugar, cada trilha roda isolada: a A toca uma
faixa embutida no binário, a B lista o que o `storage` devolver, a C substitui
a implementação de mentira pela do cartão sem tocar em ninguém.

| Trilha | Escopo | Rótulo |
|---|---|---|
| **A** | Áudio e temporização: síntese, parser RTTTL, agendamento, medições | `trilha-a-audio` |
| **B** | Interface e entrada: LVGL, telas, encoder, botões | `trilha-b-ui` |
| **C** | Armazenamento, plataforma e integração: cartão, falhas, build, CI | `trilha-c-storage` |

Os contratos estão nas issues com o rótulo `contrato` e são **pré-requisito de
todas as outras**. A etapa E0 também bloqueia todo mundo.

| Contrato | Cabeçalho |
|---|---|
| Metadados de faixa | [`include/vitrolinha/track.h`](include/vitrolinha/track.h) |
| Comandos de transporte | [`include/vitrolinha/cmd.h`](include/vitrolinha/cmd.h) |
| Instantâneo do player | [`include/vitrolinha/snapshot.h`](include/vitrolinha/snapshot.h) |
| Biblioteca e carga | [`include/vitrolinha/storage.h`](include/vitrolinha/storage.h) |

As etapas E0 a E7 estão como marcos. E0 a E5 são o produto mínimo viável.

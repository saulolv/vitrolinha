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

```
west build -b zbook@p2/rp2350b/m33
```

**O sufixo `@p2` é obrigatório.** Sem ele o build recai na revisão P1 da placa,
que não tem encoder nem display — e não emite erro nenhum.

## Documentos

| Documento | Para quê |
|---|---|
| [`docs/especificacao-vitrolinha.md`](docs/especificacao-vitrolinha.md) | Especificação v3: requisitos, arquitetura, métricas, plano de entregas |
| [`CLAUDE.md`](CLAUDE.md) | Plano de trabalho: pinos verificados, Kconfig, overlay, armadilhas, restrições de código |

Leia o `CLAUDE.md` antes de escrever a primeira linha. Ele lista três
configurações que, se omitidas, quebram o projeto **em silêncio**.

## Como o trabalho está dividido

Três trilhas paralelas, costuradas por quatro contratos congelados antes da
divisão. Com os contratos no lugar, cada trilha roda isolada: a A toca uma
constante compilada no binário, a B lista um vetor fixo, a C imprime a biblioteca
no log.

| Trilha | Escopo | Rótulo |
|---|---|---|
| **A** | Áudio e temporização: síntese, parser RTTTL, agendamento, medições | `trilha-a-audio` |
| **B** | Interface e entrada: LVGL, telas, encoder, botões | `trilha-b-ui` |
| **C** | Armazenamento, plataforma e integração: cartão, falhas, build, CI | `trilha-c-storage` |

Os contratos estão nas issues com o rótulo `contrato` e são **pré-requisito de
todas as outras**. A etapa E0 também bloqueia todo mundo.

As etapas E0 a E7 estão como marcos. E0 a E5 são o produto mínimo viável.

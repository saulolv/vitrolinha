# Testes do parser CSV

O parser recebe bytes já carregados em memória. Estes testes não abrem
arquivos durante a execução, não acessam cartão e não produzem áudio.
Os três exemplos em `fixtures/music/` são incorporados ao binário de teste
como vetores de bytes, sem terminador NUL.

## Contrato exercitado

- Cabeçalho exato `frequency_hz,duration_ms`, seguido de ao menos uma linha.
- Inteiros decimais sem sinal, espaços, frações ou campos entre aspas.
- Frequência zero representa silêncio; duração deve ser positiva.
- LF, CRLF e última linha sem quebra são aceitos; CR isolado é rejeitado.
- Frequência limitada a `UINT32_MAX`; duração total limitada a 4294967 ms.
- A inicialização valida o arquivo inteiro e informa linha/coluna do erro.
- Cada linha produz um evento no início de sua duração; ao final há silêncio.
- Notas repetidas são preservadas, sem pausas ou transposição automáticas.
- O reinício reutiliza o buffer imutável e preserva os metadados.
- Falha de inicialização invalida o iterador; nenhum evento parcial é entregue.

As frequências aceitas pelo parser não são uma promessa de suporte físico
do buzzer. Essa verificação pertence à futura implementação de `tone`.

## Executar

As suítes desta trilha (`parser_csv`, `playback`, `storage_host`) declaram
as duas variantes do simulador, `native_sim` (32 bits) e
`native_sim/native/64`. O `scripts/test.sh` e o CI rodam todas as suítes em
`native_sim`, com cobertura e com o portão `scripts/coverage_gate.py`:

```bash
scripts/test.sh               # todas as suítes, cobertura e portão
scripts/test.sh --no-coverage # só os testes
```

Para rodar só esta suíte, dentro do workspace (`scripts/zephyr.sh bash`):

```bash
west twister -p native_sim -T vitrolinha/tests/parser_csv   --outdir twister-out-parser-csv --inline-logs
```

Trocar `-p native_sim` por `-p native_sim/native/64` roda a variante de 64
bits; repetir `-p` roda as duas. É possível acrescentar `--enable-ubsan` para
verificar comportamento indefinido.

O portão de cobertura mede todo `src/`, inclusive `loader`, `player`,
`storage_host`, `tone_trace` e `playback_trace`, que estes testes e os de
`tests/playback/` e `tests/storage_host/` exercitam.

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

## Executar no workspace

```bash
west twister -p native_sim/native/64 \
  -T vitrolinha/tests/parser_csv \
  --outdir twister-out-parser-csv --inline-logs
```

Para executar também as suítes existentes na variante de 64 bits:

```bash
west twister -p native_sim/native/64 --force-platform \
  -T vitrolinha/tests \
  --outdir twister-out-csv-regression --inline-logs
```

É possível acrescentar `--enable-ubsan` para verificar comportamento
indefinido. Para cobertura, disponibilize `gcovr` no PATH e acrescente
`--coverage --coverage-tool gcovr --coverage-basedir vitrolinha`.
Com esta versão do Twister, defina `ZEPHYR_TOOLCHAIN_VARIANT=zephyr` ao
executar com cobertura, pois a seleção do gcov consulta essa variável.

O portão global de cobertura ainda exige a integração dos outros cinco
esqueletos em `src/` (`loader`, `player`, `storage_host`, `tone_trace` e
`playback_trace`). Eles não foram implementados nem excluídos da medição
como parte desta etapa do parser.

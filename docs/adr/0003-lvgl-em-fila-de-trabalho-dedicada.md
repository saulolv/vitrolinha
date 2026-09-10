# O laço do LVGL roda numa fila de trabalho dedicada, em prioridade 10

O plano de trabalho destacava `CONFIG_LV_Z_LVGL_WORKQUEUE_PRIORITY=10` como
uma das três configurações que quebram o projeto em silêncio: o padrão é 0, a
maior prioridade preemptível, o que poria o laço gráfico acima do `player`.
Ao transcrever o `prj.conf` descobrimos que o símbolo vive dentro de
`if LV_Z_RUN_LVGL_ON_WORKQUEUE`, cujo padrão é **n** — ou seja, a prioridade
crítica não tinha efeito nenhum sem que a fila de trabalho fosse ligada.

Decidimos ligar `CONFIG_LV_Z_RUN_LVGL_ON_WORKQUEUE=y` e manter a prioridade
10. A alternativa era deixar a fila desligada e escrever um laço de
`lv_timer_handler()` numa thread `ui` própria, com prioridade sob controle da
aplicação.

## Considered Options

- **Thread `ui` da aplicação chamando `lv_timer_handler()`.** É a leitura
  literal da tabela de threads da especificação, e daria controle direto
  sobre a taxa de quadros, que a E2 vai querer medir. Rejeitado por ora
  porque exige também gerir a exclusão de acesso ao LVGL na mão, e porque a
  taxa continua ajustável pela fila de trabalho.

## Consequences

`LV_Z_RUN_LVGL_ON_WORKQUEUE` implica `LV_Z_AUTO_INIT` e `LV_Z_LVGL_MUTEX`, de
modo que a inicialização do LVGL e o travamento da API passam a ser do
módulo, não da aplicação. Quem for mexer na tela na E2 usa as funções de
trava do LVGL do Zephyr em vez de assumir thread única.

Se a medição da E2 mostrar que a taxa de quadros precisa de controle mais
fino, a volta atrás é local: desligar a opção e escrever o laço na thread
`ui`. Nada fora do módulo `ui` depende de qual das duas está em uso.

Não há risco de a configuração voltar a ficar inerte sem ninguém ver: o
Zephyr aborta a configuração quando um símbolo do `prj.conf` não tem suas
dependências satisfeitas, e foi assim que o problema apareceu.

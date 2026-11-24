# Módulos do Vitrolinha.
#
# Este arquivo é a ÚNICA lista de fontes do projeto. A aplicação e cada teste
# unitário o incluem com `add_subdirectory`, de modo que o que roda na bancada
# de testes é exatamente o que roda na placa — sem uma segunda lista para
# manter em sincronia.
#
#   add_subdirectory(${VITROLINHA_ROOT}/src vitrolinha_src)
#
# O `main.c` NÃO entra aqui: ele é acrescentado pelo CMakeLists.txt da raiz.
# É o que permite aos testes compilarem os módulos sem arrastar junto um
# segundo `main()`, que colidiria com o do ztest.
#
# As fontes entram em `app` em vez de numa `zephyr_library`: em modo
# aplicação, uma zephyr_library não recebe as dependências dos cabeçalhos
# gerados do kernel e falha ao compilar qualquer arquivo que inclua
# <zephyr/kernel.h>.

# Cabeçalhos públicos dos módulos, visíveis para quem incluir este diretório.
zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/../include)

target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/cmd.c
  ${CMAKE_CURRENT_LIST_DIR}/library.c
  ${CMAKE_CURRENT_LIST_DIR}/seqlock.c
  ${CMAKE_CURRENT_LIST_DIR}/snapshot.c
  ${CMAKE_CURRENT_LIST_DIR}/storage_error.c
  ${CMAKE_CURRENT_LIST_DIR}/track.c
  ${CMAKE_CURRENT_LIST_DIR}/parser_csv.c
  ${CMAKE_CURRENT_LIST_DIR}/parser_rtttl.c
  ${CMAKE_CURRENT_LIST_DIR}/parser.c
)

# Os botões de transporte. Condicional porque escutam o subsistema `input`,
# que só a aplicação e os testes de entrada ligam.
target_sources_ifdef(CONFIG_INPUT app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/buttons.c
)

# A interface. Condicional porque depende do LVGL, que só a aplicação liga: na
# lista comum, cada suíte de teste tentaria compilá-la sem os cabeçalhos dele.
target_sources_ifdef(CONFIG_LVGL app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/display.c
)

# A saída de tom: na placa, o buzzer por PWM; no simulador, a captura em
# memória.
#
# Onde há PWM, ele é a saída — na placa, o buzzer; em tests/tone_pwm/, o PWM
# de mentira do Zephyr. A captura só entra onde não há: o native_sim liga a
# libc do computador por padrão, e sem esta precedência o teste do PWM
# testaria a captura sem perceber.
if(CONFIG_BOARD_NATIVE_SIM AND CONFIG_EXTERNAL_LIBC)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/playback_trace.c)
endif()

if(CONFIG_PWM)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/tone_pwm.c)
elseif(CONFIG_BOARD_NATIVE_SIM AND CONFIG_EXTERNAL_LIBC)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/tone_trace.c)
endif()

# O player, só onde há uma saída de tom e `CONFIG_POLL`. Quem decide é
# `CONFIG_VITROLINHA_PLAYER`, no Kconfig.vitrolinha — a mesma opção que o
# src/main.c consulta, para que os dois nunca discordem. A omissão não é
# silenciosa: quem chamar `player_init()` sem a opção não linka.
#
# O loader entra junto do player, que é o único a chamá-lo, e não na lista
# comum: ele define uma thread com pilha própria, o buffer de 4 KB da faixa e
# a fila de 32 eventos — perto de 9 KB de RAM que ficariam reservados para
# uma thread que nunca parte. O parser_csv fica na lista comum porque não tem
# estado estático: sem chamador, não custa RAM.
target_sources_ifdef(CONFIG_VITROLINHA_PLAYER app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/player.c
  ${CMAKE_CURRENT_LIST_DIR}/loader.c
)

# Quem responde ao storage.h: o cartão, a implementação de mentira com duas
# faixas embutidas no binário, ou os CSVs de uma pasta do computador. A escolha
# é do Kconfig.vitrolinha — cartão na placa, mentira no simulador, que não tem
# disco; a pasta só existe no simulador e só quando pedida.
#
# O `else` é a mentira, e não a ausência de storage, por causa dos testes: um
# teste só enxerga as opções do projeto se tiver um Kconfig próprio que as
# inclua, como tests/storage_card/ e tests/storage_host/. Os demais não
# enxergam a escolha e caem aqui, com a mentira — que é o que eles usavam
# antes de ela existir.
if(CONFIG_VITROLINHA_STORAGE_HOST)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/storage_host.c)
elseif(CONFIG_VITROLINHA_STORAGE_CARD)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/storage.c)
else()
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/storage_fake.c)
endif()

# O cartão. Condicional porque o módulo monta um volume FAT: sem o sistema de
# arquivos ele não teria o que fazer, e arrastá-lo para dentro dos testes dos
# outros módulos obrigaria cada um deles a configurar uma pilha de disco que
# não usa. A omissão não é silenciosa — quem chamar `card_init()` sem esta
# opção não linka.
#
# `ELMFAT` é a biblioteca de interface que o módulo FatFs exporta: dela vêm o
# `ff.h`, de onde sai o tipo `FATFS` do objeto de trabalho do volume, e o
# `ZEPHYR_CONFIG_OVERRIDE` que faz esse cabeçalho enxergar a configuração do
# Zephyr.
if(CONFIG_FAT_FILESYSTEM_ELM)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/card.c)
  target_link_libraries(app PRIVATE ELMFAT)
endif()

# Só no simulador: converte as setas do teclado em passos de encoder, papel
# que na placa é do driver gpio-qdec.
target_sources_ifdef(CONFIG_BOARD_NATIVE_SIM app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/sim_encoder.c
)

# A última faixa lembrada (RF10). Só onde o projeto pede e há `settings`:
# a opção depende de CONFIG_SETTINGS, e os testes que não a enxergam ficam
# sem o módulo — e sem a thread dele.
target_sources_ifdef(CONFIG_VITROLINHA_LAST_TRACK app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/last_track.c
)

# Os LEDs de estado (RF11). Só onde há fita no alias `led-strip`: na placa,
# os WS2812; nos testes, a fita de mentira de tests/status_led/.
target_sources_ifdef(CONFIG_VITROLINHA_STATUS_LED app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/status_led.c
)

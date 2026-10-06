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
  ${CMAKE_CURRENT_LIST_DIR}/seqlock.c
  ${CMAKE_CURRENT_LIST_DIR}/snapshot.c
  ${CMAKE_CURRENT_LIST_DIR}/track.c
)

# Quem responde ao storage.h: o cartão ou a implementação de mentira, com duas
# faixas embutidas no binário. A escolha é do Kconfig.vitrolinha — cartão na
# placa, mentira no simulador, que não tem disco.
#
# O `else` é a mentira, e não a ausência de storage, por causa dos testes: um
# teste só enxerga as opções do projeto se tiver um Kconfig próprio que as
# inclua, e só tests/storage_card/ tem. Os demais não enxergam a escolha e
# caem aqui, com a mentira — que é o que eles usavam antes de ela existir.
if(CONFIG_VITROLINHA_STORAGE_CARD)
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

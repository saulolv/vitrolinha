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

# Implementação de mentira do storage: duas faixas embutidas no binário.
# Quando o storage do cartão existir (issue #9), vira uma escolha entre as
# duas; hoje há uma só, e antecipar o Kconfig da escolha seria configurar um
# seletor de uma opção.
target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/storage_fake.c
)

# Só no simulador: converte as setas do teclado em passos de encoder, papel
# que na placa é do driver gpio-qdec.
target_sources_ifdef(CONFIG_BOARD_NATIVE_SIM app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/sim_encoder.c
)

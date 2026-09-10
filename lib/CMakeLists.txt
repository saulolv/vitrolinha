# Módulos do Vitrolinha.
#
# Este arquivo é a ÚNICA lista de fontes do projeto. A aplicação e cada teste
# unitário o incluem com `add_subdirectory`, de modo que o que roda na bancada
# de testes é exatamente o que roda na placa — sem uma segunda lista para
# manter em sincronia.
#
#   add_subdirectory(${VITROLINHA_ROOT}/lib vitrolinha_lib)
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
# Na E3 vira uma escolha entre esta e storage_sd.c; hoje há uma só, e
# antecipar o Kconfig da escolha seria configurar um seletor de uma opção.
target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/storage_fake.c
)

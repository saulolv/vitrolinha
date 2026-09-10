# Aplicação Vitrolinha.
#
# Compilar:  scripts/build.sh
#     ou:    west build -b zbook@p2/rp2350b/m33

cmake_minimum_required(VERSION 3.20.0)

# ----------------------------------------------------------------------------
# Trava de revisão da placa.
#
# Esta é a armadilha central do projeto. O board.yml da ZBook declara
# `default: p2`, mas esse campo é ignorado em placas de formato `custom`, e o
# revision.cmake da placa recai em `p1` quando BOARD_REVISION não vem
# definido. Um binário para P1 sai sem encoder, sem display e com conflitos de
# pino — e, num projeto sem app.overlay, sairia sem erro nenhum.
#
# A verificação é feita duas vezes de propósito:
#
#  - Aqui, antes do find_package, sobre a string do alvo. É o único ponto em
#    que ainda dá para emitir uma mensagem útil: o app.overlay referencia nós
#    que só existem no P2, então um alvo errado morreria lá dentro com um erro
#    de dtc sobre rótulo inexistente.
#  - Abaixo, depois do find_package, sobre BOARD_REVISION já interpretada pelo
#    Zephyr. Pega o caso de o alvo chegar por um caminho que esta primeira não
#    enxerga.
#
# Em src/main.c há ainda uma terceira, por BUILD_ASSERT sobre o devicetree.
# ----------------------------------------------------------------------------
set(vitrolinha_expected_board "zbook@p2/rp2350b/m33")
set(vitrolinha_board_hint "${BOARD}")
if(NOT vitrolinha_board_hint)
  set(vitrolinha_board_hint "$ENV{BOARD}")
endif()

if(vitrolinha_board_hint AND NOT "${vitrolinha_board_hint}" MATCHES "^zbook@p2(/|$)")
  message(FATAL_ERROR
    "Alvo inválido: '${vitrolinha_board_hint}'.\n"
    "O Vitrolinha só compila para a revisão P2 da ZBook:\n"
    "    west build -b ${vitrolinha_expected_board}\n"
    "O sufixo @p2 é obrigatório: sem ele o encoder, o display e o "
    "card-detect não existem no devicetree, e o board.yml da placa não "
    "supre o padrão.")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(vitrolinha
        VERSION 0.1.0
        DESCRIPTION "Tocador de músicas embarcado sobre Zephyr RTOS"
        LANGUAGES C)

if(NOT "${BOARD}" STREQUAL "zbook" OR NOT "${BOARD_REVISION}" STREQUAL "p2")
  message(FATAL_ERROR
    "Revisão de placa inválida: BOARD='${BOARD}' "
    "BOARD_REVISION='${BOARD_REVISION}'.\n"
    "Compile com: west build -b ${vitrolinha_expected_board}")
endif()

# Módulos do projeto. A lista de fontes e o diretório de cabeçalhos moram em
# lib/CMakeLists.txt, de onde os testes unitários também os consomem.
add_subdirectory(lib)

target_sources(app PRIVATE src/main.c)

# Aplicação Vitrolinha.
#
# Compilar:  scripts/build.sh
#     ou:    west build -b zbook@p2/rp2350b/m33

cmake_minimum_required(VERSION 3.20.0)

# ----------------------------------------------------------------------------
# Alvos de compilação suportados.
#
# Dois, de propósito:
#
#   zbook@p2/rp2350b/m33   a placa
#   native_sim             o simulador, com OLED em janela SDL e botões no
#                          teclado, para as trilhas de interface e de
#                          armazenamento não ficarem na fila do hardware
#
# A ZBook exige o sufixo @p2, e essa é a armadilha central do projeto: o
# board.yml declara `default: p2`, mas o campo é ignorado em placas de formato
# `custom` e o revision.cmake recai em `p1`. Um binário para P1 sai sem
# encoder, sem display e com conflitos de pino — e, num projeto sem overlay
# próprio, sairia sem erro nenhum.
#
# A verificação é feita duas vezes:
#
#  - Aqui, antes do find_package, sobre a string do alvo. É o único ponto em
#    que ainda dá para emitir mensagem útil: o overlay da placa referencia nós
#    que só existem no P2, e um alvo errado morreria lá dentro com erro de dtc
#    sobre rótulo inexistente.
#  - Abaixo, depois do find_package, sobre BOARD_REVISION já interpretada.
#
# Em src/board_zbook.h há ainda uma terceira, por BUILD_ASSERT.
# ----------------------------------------------------------------------------
set(vitrolinha_board_hint "${BOARD}")
if(NOT vitrolinha_board_hint)
  set(vitrolinha_board_hint "$ENV{BOARD}")
endif()

if(vitrolinha_board_hint
   AND NOT "${vitrolinha_board_hint}" MATCHES "^zbook@p2(/|$)"
   AND NOT "${vitrolinha_board_hint}" STREQUAL "native_sim/native/64")
  message(FATAL_ERROR
    "Alvo inválido: '${vitrolinha_board_hint}'.\n"
    "O Vitrolinha compila para dois alvos, e só para estes dois:\n"
    "    west build -b zbook@p2/rp2350b/m33   (a placa)\n"
    "    west build -b native_sim/native/64   (o simulador)\n"
    "\n"
    "Na ZBook, o sufixo @p2 é obrigatório: sem ele o encoder, o display e o "
    "card-detect não existem no devicetree, e o board.yml da placa não supre "
    "o padrão.\n"
    "\n"
    "No simulador, a variante /native/64 também é obrigatória, por dois "
    "motivos: o nome dos arquivos em boards/ tem de casar com a string de "
    "build, e o native_sim de 32 bits exigiria a SDL em i386.")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# A versão não fica aqui: o Zephyr a lê do arquivo VERSION na raiz e gera
# <app_version.h>, que é como o firmware a imprime no console. Declará-la
# também em project() criaria duas fontes para o mesmo número.
project(vitrolinha
        DESCRIPTION "Tocador de músicas embarcado sobre Zephyr RTOS"
        LANGUAGES C)

if("${BOARD}" STREQUAL "zbook" AND NOT "${BOARD_REVISION}" STREQUAL "p2")
  message(FATAL_ERROR
    "Revisão de placa inválida: BOARD_REVISION='${BOARD_REVISION}'.\n"
    "Compile com: west build -b zbook@p2/rp2350b/m33")
endif()

# Módulos do projeto. A lista de fontes e o diretório de cabeçalhos moram em
# src/CMakeLists.txt, de onde os testes unitários também os consomem.
add_subdirectory(src)

# O ponto de entrada fica de fora daquela lista: os testes trazem os módulos
# sem trazer um segundo main().
target_sources(app PRIVATE src/main.c)

# Bancada do RNF03: custo de abrir e ler uma faixa do cartão.
#
# É uma aplicação Zephyr separada, e não um teste nem parte do firmware. As
# três razões:
#
#  - O resultado é um relatório para ler, não uma asserção que passa ou falha.
#  - A configuração é outra: aqui o cartão é formatado em tempo de execução,
#    o que exige FILE_SYSTEM_MKFS — código que o firmware não deve conter.
#  - Fora de src/, ela não entra no portão de cobertura, que exige 100% de
#    linhas e ramos de todo arquivo da aplicação. Uma bancada não se testa
#    unitariamente; ela É o teste.
#
# Rodar:  scripts/bench.sh
#    ou:  west build -b native_sim/native/64 bench/rnf03 && ./build/zephyr/zephyr.exe

cmake_minimum_required(VERSION 3.20.0)

# A mesma armadilha do firmware, pelo mesmo motivo: sem o sufixo @p2 o build
# recai numa placa sem encoder e sem display, e aqui recairia também num
# devicetree sem o nó do cartão.
set(bench_board_hint "${BOARD}")
if(NOT bench_board_hint)
  set(bench_board_hint "$ENV{BOARD}")
endif()

if(bench_board_hint
   AND NOT "${bench_board_hint}" MATCHES "^zbook@p2(/|$)"
   AND NOT "${bench_board_hint}" STREQUAL "native_sim/native/64")
  message(FATAL_ERROR
    "Alvo inválido: '${bench_board_hint}'.\n"
    "A bancada compila para os mesmos dois alvos do firmware:\n"
    "    west build -b native_sim/native/64 bench/rnf03   (o simulador)\n"
    "    west build -b zbook@p2/rp2350b/m33 bench/rnf03   (a placa)")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(vitrolinha_bench_rnf03
        DESCRIPTION "Custo de fs_open e leitura, para o orçamento do RNF03"
        LANGUAGES C)

# Os contratos da aplicação, só pelos cabeçalhos: a bancada tira daí o teto da
# biblioteca e o do buffer de faixa, em vez de repetir os números. Nenhuma
# fonte de src/ é compilada aqui — a bancada mede o sistema de arquivos, não os
# módulos.
zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/../../include)

target_sources(app PRIVATE main.c)

# O cartão de mentira só existe no simulador. Na placa quem responde é o
# cartão de verdade, e sim_card.h vira um punhado de funções vazias.
target_sources_ifdef(CONFIG_BOARD_NATIVE_SIM app PRIVATE sim_card.c)

# `ff.h`, para o MKFS_PARM que descreve a geometria do volume formatado. O
# módulo FatFs exporta os diretórios de cabeçalho — e o `ZEPHYR_CONFIG_OVERRIDE`
# que faz o `ff.h` enxergar a configuração do Zephyr — nesta biblioteca de
# interface.
target_link_libraries(app PRIVATE ELMFAT)

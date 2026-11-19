cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(vitrolinha_parser_rtttl_test LANGUAGES C)

include(${CMAKE_CURRENT_LIST_DIR}/../vitrolinha_test.cmake)
target_sources(app PRIVATE src/main.c)

# Exercita os arquivos de exemplo reais, incorporados como bytes sem NUL. O
# escala.csv entra para a fachada provar que reconhece os dois formatos.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
foreach(fixture furelise.rtx odetojoy.rtx escala.csv)
  generate_inc_file_for_target(app
    ${CMAKE_CURRENT_LIST_DIR}/../../fixtures/music/${fixture}
    ${CMAKE_CURRENT_BINARY_DIR}/${fixture}.inc)
endforeach()

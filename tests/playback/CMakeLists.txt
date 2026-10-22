cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(vitrolinha_playback_test LANGUAGES C)
set(VITROLINHA_STORAGE_HOST ON)
include(${CMAKE_CURRENT_LIST_DIR}/../vitrolinha_test.cmake)
target_sources(app PRIVATE src/main.c)
foreach(fixture escala pausas notas_repetidas)
  generate_inc_file_for_target(app
    ${CMAKE_CURRENT_LIST_DIR}/../../fixtures/music/${fixture}.csv
    ${CMAKE_CURRENT_BINARY_DIR}/${fixture}.inc)
endforeach()
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

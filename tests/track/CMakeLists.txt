cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(vitrolinha_track_test LANGUAGES C)

include(${CMAKE_CURRENT_LIST_DIR}/../vitrolinha_test.cmake)

target_sources(app PRIVATE src/main.c)

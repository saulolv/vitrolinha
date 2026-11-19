cmake_minimum_required(VERSION 3.20.0)
# O display de teste tem binding próprio, em dts/bindings.
list(APPEND DTS_ROOT ${CMAKE_CURRENT_LIST_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(vitrolinha_display_test LANGUAGES C)
include(${CMAKE_CURRENT_LIST_DIR}/../vitrolinha_test.cmake)
target_sources(app PRIVATE
  src/fake_display.c
  src/main.c
)

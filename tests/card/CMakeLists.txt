cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(vitrolinha_card_test LANGUAGES C)

# Exercita o backend SD sobre o mesmo FatFs/disco emulado do monitor.
set(VITROLINHA_STORAGE_SD ON)
include(${CMAKE_CURRENT_LIST_DIR}/../vitrolinha_test.cmake)

target_sources(app PRIVATE
  src/fake_disk.c
  src/main.c
)

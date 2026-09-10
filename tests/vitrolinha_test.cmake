# Base comum dos testes unitários.
#
# Cada teste é uma aplicação Zephyr independente. Este arquivo é o que faz
# todas elas compilarem os módulos a partir da mesma lista de fontes que o
# firmware usa — nenhum teste declara fontes de módulo por conta própria.
#
# Uso, no CMakeLists.txt do teste:
#
#   include(${CMAKE_CURRENT_LIST_DIR}/../vitrolinha_test.cmake)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../lib vitrolinha_lib)

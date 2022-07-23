cmake_minimum_required(VERSION 3.16)
project (umq)
set (CMAKE_CXX_STANDARD 17)

EXEC_PROGRAM("git submodule update --init")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/)

include_directories(BEFORE tests)

add_compile_options(-Wall -Wno-noexcept-type)
add_library (umq dummy.cpp)

add_subdirectory (tests)
add_subdirectory (tests/userver)


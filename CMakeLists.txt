cmake_minimum_required(VERSION 3.16)
project(shell_emulator CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

add_executable(emulator src/main.cpp)

add_executable(test_bin tests/test_repl.cpp)

enable_testing()
add_test(NAME ReplTests COMMAND test_bin)

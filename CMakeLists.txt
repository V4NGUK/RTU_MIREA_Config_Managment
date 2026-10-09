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

if(MINGW)
    get_filename_component(_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_mingw_prefix "${_mingw_bin}/.." ABSOLUTE)
    list(PREPEND CMAKE_PREFIX_PATH "${_mingw_prefix}")
endif()

find_package(ZLIB REQUIRED)

add_library(vfs_storage src/vfs.cpp)
target_include_directories(vfs_storage PUBLIC src)
target_link_libraries(vfs_storage PUBLIC ZLIB::ZLIB)

add_executable(emulator src/main.cpp)
target_link_libraries(emulator PRIVATE vfs_storage)

set(VFS_LIBRARY vfs_storage)
set(VFS_EXAMPLE_NAMES minimal multiple-files nested)
include(cmake/Examples.cmake)

add_executable(test_bin tests/test_repl.cpp)

enable_testing()
add_test(NAME ReplTests COMMAND test_bin)

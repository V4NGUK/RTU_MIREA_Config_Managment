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

add_library(shell_core
    src/vfs.cpp
    src/vfs_ops.cpp
    src/parser.cpp
    src/permissions.cpp
    src/shell.cpp
    src/text_commands.cpp
)
target_include_directories(shell_core PUBLIC src)
target_link_libraries(shell_core PUBLIC ZLIB::ZLIB)

add_executable(emulator src/main.cpp)
target_link_libraries(emulator PRIVATE shell_core)

set(VFS_LIBRARY shell_core)
set(VFS_EXAMPLE_NAMES minimal multiple-files nested commands)
include(cmake/Examples.cmake)

add_executable(test_bin tests/test_repl.cpp)
target_link_libraries(test_bin PRIVATE shell_core)

enable_testing()
add_test(NAME ReplTests COMMAND test_bin)

add_executable(command_tests tests/test_commands.cpp)
target_link_libraries(command_tests PRIVATE shell_core)
add_dependencies(command_tests example_archives)
if(MSVC)
    target_compile_options(command_tests PRIVATE /utf-8)
endif()
add_test(NAME CommandTests COMMAND command_tests
    "${VFS_EXAMPLE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/test-output")
add_test(NAME CliChecks COMMAND ${CMAKE_COMMAND}
    "-DEMULATOR=$<TARGET_FILE:emulator>" "-DFIXTURE_DIR=${VFS_EXAMPLE_DIR}"
    "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-output" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_checks.cmake")

add_executable(mutation_tests tests/test_mutations.cpp)
target_link_libraries(mutation_tests PRIVATE shell_core)
add_dependencies(mutation_tests example_archives)
add_test(NAME MutationTests COMMAND mutation_tests
    "${VFS_EXAMPLE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/mutation-output")
if(WIN32)
    find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell)
    if(POWERSHELL_EXECUTABLE)
        add_test(NAME ZipInterop COMMAND ${POWERSHELL_EXECUTABLE} -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/zip_interop.ps1" -Emulator $<TARGET_FILE:emulator>
            -Fixtures "${VFS_EXAMPLE_DIR}" -Work "${CMAKE_CURRENT_BINARY_DIR}/interop-output")
    endif()
endif()

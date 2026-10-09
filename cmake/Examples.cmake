add_executable(vfs_examples tools/vfs_examples.cpp)
target_link_libraries(vfs_examples PRIVATE ${VFS_LIBRARY})
if(MSVC)
    target_compile_options(vfs_examples PRIVATE /utf-8)
endif()
set(VFS_EXAMPLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/vfs")
set(VFS_EXAMPLE_FILES)
foreach(name IN LISTS VFS_EXAMPLE_NAMES)
    list(APPEND VFS_EXAMPLE_FILES "${VFS_EXAMPLE_DIR}/${name}.zip")
endforeach()
add_custom_command(OUTPUT ${VFS_EXAMPLE_FILES}
    COMMAND $<TARGET_FILE:vfs_examples> "${VFS_EXAMPLE_DIR}"
    DEPENDS vfs_examples
    COMMENT "Creating ZIP examples in the build directory"
    VERBATIM)
add_custom_target(example_archives ALL DEPENDS ${VFS_EXAMPLE_FILES})
add_dependencies(emulator example_archives)

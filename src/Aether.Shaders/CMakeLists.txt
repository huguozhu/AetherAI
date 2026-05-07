add_library(Aether.Shaders STATIC)

target_sources(Aether.Shaders
    PUBLIC
        FILE_SET CXX_MODULES
        FILES
            src/aether.shaders.cppm
    PRIVATE
        src/shader_compiler.cpp
        src/shader_library.cpp
        src/slang_backend.cpp
)

target_link_libraries(Aether.Shaders PUBLIC Aether.Core)
target_include_directories(Aether.Shaders PUBLIC src)

# Slang SDK integration — slang_DIR is set by the root CMakeLists.txt.
if(NOT TARGET slang::slang)
    if(NOT EXISTS "${slang_DIR}/slangConfig.cmake")
        message(FATAL_ERROR "Slang SDK: config not found at ${slang_DIR}")
    endif()
    include("${slang_DIR}/slangConfig.cmake")
endif()
target_link_libraries(Aether.Shaders PRIVATE slang::slang)
target_compile_definitions(Aether.Shaders PRIVATE AETHER_SLANG_AVAILABLE=1)

# Derive SDK paths from slang_DIR (which points to the cmake/ directory)
get_filename_component(_slang_root "${slang_DIR}/.." ABSOLUTE)
set(_slang_bin "${_slang_root}/bin")

message(STATUS "Aether.Shaders: Slang SDK found at ${_slang_root}")

# Copy runtime DLLs to output directory
if(WIN32 AND EXISTS "${_slang_bin}/slang.dll")
    file(GLOB _slang_modules "${_slang_bin}/slang-standard-module-*")
    add_custom_command(TARGET Aether.Shaders POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_slang_bin}/slang.dll"
            "$<TARGET_FILE_DIR:Aether.Shaders>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_slang_bin}/slang-compiler.dll"
            "$<TARGET_FILE_DIR:Aether.Shaders>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_slang_bin}/slang-llvm.dll"
            "$<TARGET_FILE_DIR:Aether.Shaders>"
        COMMENT "Copying Slang runtime DLLs to output directory"
    )
    foreach(_mod_dir IN LISTS _slang_modules)
        get_filename_component(_mod_name "${_mod_dir}" NAME)
        add_custom_command(TARGET Aether.Shaders POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_mod_dir}"
                "$<TARGET_FILE_DIR:Aether.Shaders>/${_mod_name}"
        )
    endforeach()
endif()

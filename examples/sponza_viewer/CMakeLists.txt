add_executable(SponzaViewerExample)
set_target_properties(SponzaViewerExample PROPERTIES FOLDER "Examples")
target_sources(SponzaViewerExample PRIVATE main.cpp)
target_link_libraries(SponzaViewerExample PRIVATE Aether.Gltf Aether.Renderer Aether.Shaders Aether.RHI Aether.Core)
target_link_libraries(SponzaViewerExample PRIVATE d3d12.lib dxgi.lib d3dcompiler.lib)

# Copy Slang runtime DLLs to example output directory
find_package(slang CONFIG QUIET HINTS "${slang_DIR}")
if(slang_FOUND AND WIN32)
    get_filename_component(_slang_root "${slang_DIR}/.." ABSOLUTE)
    set(_slang_bin "${_slang_root}/bin")
    if(EXISTS "${_slang_bin}/slang.dll")
        add_custom_command(TARGET SponzaViewerExample POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_slang_bin}/slang.dll"
                "$<TARGET_FILE_DIR:SponzaViewerExample>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_slang_bin}/slang-compiler.dll"
                "$<TARGET_FILE_DIR:SponzaViewerExample>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_slang_bin}/slang-llvm.dll"
                "$<TARGET_FILE_DIR:SponzaViewerExample>"
            COMMENT "Copying Slang runtime DLLs to example output directory"
        )
        file(GLOB _slang_modules "${_slang_bin}/slang-standard-module-*")
        foreach(_mod_dir IN LISTS _slang_modules)
            get_filename_component(_mod_name "${_mod_dir}" NAME)
            add_custom_command(TARGET SponzaViewerExample POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_mod_dir}"
                    "$<TARGET_FILE_DIR:SponzaViewerExample>/${_mod_name}"
            )
        endforeach()
    endif()
endif()

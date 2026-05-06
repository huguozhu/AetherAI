add_library(Aether.Shaders STATIC)

target_sources(Aether.Shaders
    PUBLIC
        FILE_SET CXX_MODULES
        FILES
            src/aether.shaders.cppm
    PRIVATE
        src/shader_compiler.cpp
        src/shader_library.cpp
)

target_link_libraries(Aether.Shaders PUBLIC Aether.Core)
target_include_directories(Aether.Shaders PUBLIC src)

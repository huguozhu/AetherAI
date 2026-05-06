add_library(Aether.Renderer STATIC)

target_sources(Aether.Renderer
    PUBLIC
        FILE_SET CXX_MODULES
        FILES
            src/aether.renderer.cppm
    PRIVATE
        src/gpu_scene.cpp
        src/gpu_culling.cpp
        src/indirect_draw.cpp
        src/render_scene.cpp
)

target_link_libraries(Aether.Renderer PUBLIC Aether.Resources Aether.RHI Aether.Core)
target_include_directories(Aether.Renderer PUBLIC src)

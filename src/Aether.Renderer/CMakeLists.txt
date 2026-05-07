add_library(Aether.Renderer STATIC)
set_target_properties(Aether.Renderer PROPERTIES FOLDER "AetherEngine")

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
        src/scene_manager.cpp
        src/components/mesh_component.cpp
        src/components/light_component.cpp
        src/components/camera_component.cpp
)

target_link_libraries(Aether.Renderer PUBLIC Aether.Resources Aether.RHI Aether.Core)
target_include_directories(Aether.Renderer PUBLIC src)

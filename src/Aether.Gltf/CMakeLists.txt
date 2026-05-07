add_library(Aether.Gltf STATIC)
set_target_properties(Aether.Gltf PROPERTIES FOLDER "AetherEngine")

target_sources(Aether.Gltf
    PUBLIC
        FILE_SET CXX_MODULES
        FILES
            src/aether.gltf.cppm
    PRIVATE
        src/gltf_importer.cpp
        src/stb_image_impl.cpp
)

target_link_libraries(Aether.Gltf PUBLIC Aether.Renderer Aether.Resources Aether.RHI Aether.Core)
target_include_directories(Aether.Gltf PRIVATE "${CMAKE_SOURCE_DIR}/external/stb")
target_link_libraries(Aether.Gltf PRIVATE fastgltf::fastgltf)

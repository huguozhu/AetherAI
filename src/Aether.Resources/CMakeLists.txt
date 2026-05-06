add_library(Aether.Resources STATIC)

target_sources(Aether.Resources
    PUBLIC
        FILE_SET CXX_MODULES
        FILES
            src/aether.resources.cppm
    PRIVATE
        src/resource_loader.cpp
)

target_link_libraries(Aether.Resources PUBLIC Aether.RHI Aether.Core)
target_include_directories(Aether.Resources PUBLIC src)

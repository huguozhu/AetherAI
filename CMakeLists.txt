cmake_minimum_required(VERSION 3.28)
project(AetherAI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AETHER_BUILD_EXAMPLES "Build example applications" ON)

add_subdirectory(src/Aether.Core)
add_subdirectory(src/Aether.RHI)
add_subdirectory(src/Aether.Resources)
add_subdirectory(src/Aether.Renderer)
add_subdirectory(src/Aether.Shaders)

if(AETHER_BUILD_EXAMPLES)
    add_subdirectory(examples/triangle)
endif()

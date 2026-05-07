cmake_minimum_required(VERSION 3.28)
project(AetherAI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AETHER_BUILD_EXAMPLES "Build example applications" ON)

# === External Dependencies ===

# Slang shader compiler SDK — downloaded from GitHub, or use a local copy.
set(SLANG_VERSION "2026.8")
set(SLANG_URL "https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/slang-${SLANG_VERSION}-windows-x86_64.zip")
if(EXISTS "${CMAKE_SOURCE_DIR}/external/slang/cmake/slangConfig.cmake")
    set(slang_DIR "${CMAKE_SOURCE_DIR}/external/slang/cmake")
    message(STATUS "Slang SDK: found at external/slang")
else()
    set(_slang_dest "${CMAKE_BINARY_DIR}/_deps/slang-src")
    if(NOT EXISTS "${_slang_dest}/cmake/slangConfig.cmake")
        message(STATUS "Slang SDK: downloading v${SLANG_VERSION}...")
        set(_slang_zip "${CMAKE_BINARY_DIR}/_deps/slang.zip")
        file(DOWNLOAD "${SLANG_URL}" "${_slang_zip}" SHOW_PROGRESS STATUS _dl_status)
        list(GET _dl_status 0 _dl_code)
        list(GET _dl_status 1 _dl_msg)
        if(NOT _dl_code EQUAL 0)
            message(FATAL_ERROR "Slang SDK download failed: ${_dl_msg}")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${_slang_zip}" DESTINATION "${_slang_dest}")
    endif()
    # Locate slangConfig.cmake (zip may have a top-level directory)
    file(GLOB_RECURSE _slang_config "${_slang_dest}/slangConfig.cmake")
    if(_slang_config)
        list(GET _slang_config 0 _slang_config_file)
        get_filename_component(slang_DIR "${_slang_config_file}" PATH)
    endif()
    if(NOT slang_DIR OR NOT EXISTS "${slang_DIR}/slangConfig.cmake")
        message(FATAL_ERROR "Slang SDK: not found and failed to download")
    endif()
    message(STATUS "Slang SDK: v${SLANG_VERSION} ready")
endif()
message(STATUS "Slang SDK config dir: ${slang_DIR}")

# === glTF Library ===
include(FetchContent)
FetchContent_Declare(
    fastgltf
    URL https://github.com/spnda/fastgltf/archive/refs/tags/v0.7.2.zip
    SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/fastgltf-src"
)
set(FETCHCONTENT_QUIET OFF)
set(FASTGLTF_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(fastgltf)

# === stb_image for texture loading ===
set(STB_IMAGE_DIR "${CMAKE_SOURCE_DIR}/external/stb")
if(NOT EXISTS "${STB_IMAGE_DIR}/stb_image.h")
    message(FATAL_ERROR "stb_image.h not found at ${STB_IMAGE_DIR}/stb_image.h. "
                        "Download manually from https://raw.githubusercontent.com/nothings/stb/master/stb_image.h")
endif()

add_subdirectory(src/Aether.Core)
add_subdirectory(src/Aether.RHI)
add_subdirectory(src/Aether.Resources)
add_subdirectory(src/Aether.Renderer)
add_subdirectory(src/Aether.Shaders)
add_subdirectory(src/Aether.Gltf)

if(AETHER_BUILD_EXAMPLES)
    add_subdirectory(examples/triangle)
    add_subdirectory(examples/slang_triangle)
    add_subdirectory(examples/gltf_viewer)
    add_subdirectory(examples/sponza_viewer)
endif()

add_library(Aether.RHI STATIC)
set_target_properties(Aether.RHI PROPERTIES FOLDER "AetherEngine")

target_sources(Aether.RHI
    PUBLIC
        FILE_SET CXX_MODULES
        FILES
            src/aether.rhi.cppm
    PRIVATE
        D3D12/d3d12_common.h
        D3D12/device_d3d12_impl.h
        D3D12/resource_d3d12_impl.h
        D3D12/command_list_d3d12_impl.h
        D3D12/pipeline_d3d12_impl.h
        D3D12/device_d3d12.cpp
        D3D12/command_list_d3d12.cpp
        D3D12/resource_d3d12.cpp
        D3D12/pipeline_d3d12.cpp
        D3D12/swapchain_d3d12.cpp
)

target_link_libraries(Aether.RHI PUBLIC Aether.Core)
target_include_directories(Aether.RHI PUBLIC src)

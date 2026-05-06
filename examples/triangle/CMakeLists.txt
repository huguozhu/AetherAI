add_executable(TriangleExample)
target_sources(TriangleExample PRIVATE main.cpp)
target_link_libraries(TriangleExample PRIVATE Aether.RHI Aether.Core)

# Link D3D12 and DXGI
target_link_libraries(TriangleExample PRIVATE d3d12.lib dxgi.lib d3dcompiler.lib)

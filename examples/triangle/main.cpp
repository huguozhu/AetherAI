#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <cstring>
#include <vector>
#include <memory>
#include <cstdint>
#include <cstddef>

import aether.rhi;
import aether.core;

// Global window and device
HWND g_hwnd = nullptr;
std::unique_ptr<aether::rhi::Device> g_device = nullptr;

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            // Handle resize
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Create a Win32 window
HWND create_window(HINSTANCE hInstance, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"AetherTriangleWindow";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"AetherAI - Triangle Demo",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hwnd, nCmdShow);
    return hwnd;
}

// Compile a shader from HLSL source using D3DCompile
std::vector<std::byte> compile_shader(const char* source, const char* entryPoint, const char* target) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* error = nullptr;

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(
        source, strlen(source), nullptr,
        nullptr, nullptr,
        entryPoint, target,
        flags, 0, &blob, &error);

    if (FAILED(hr)) {
        if (error) {
            aether::log::error("Shader compile error: {}",
                               static_cast<const char*>(error->GetBufferPointer()));
            error->Release();
        }
        return {};
    }

    const uint8_t* data = static_cast<const uint8_t*>(blob->GetBufferPointer());
    std::vector<std::byte> result(blob->GetBufferSize());
    memcpy(result.data(), blob->GetBufferPointer(), blob->GetBufferSize());
    blob->Release();
    return result;
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    HINSTANCE hInstance = GetModuleHandle(nullptr);

    // Create window
    g_hwnd = create_window(hInstance, SW_SHOW);
    if (!g_hwnd) {
        aether::log::error("Failed to create window");
        return 1;
    }

    // Create device
    g_device = aether::rhi::create_d3d12_device();
    if (!g_device) {
        aether::log::error("Failed to create D3D12 device");
        return 1;
    }

    // --- Simple triangle vertex data ---
    struct Vertex {
        float position[3];
        float color[3];
        float uv[2];
    };

    // Triangle centered on screen — CW winding in NDC (front face)
    Vertex triVertices[] = {
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.0f,  0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
    };

    // Create vertex buffer
    aether::rhi::BufferDesc vbDesc{};
    vbDesc.size = sizeof(triVertices);
    vbDesc.heap = aether::rhi::HeapType::Upload;
    vbDesc.bindFlags = aether::rhi::BindFlags::VertexBuffer;
    auto vertexBuffer = g_device->create_buffer(vbDesc);
    {
        void* mappedData = vertexBuffer->map();
        memcpy(mappedData, triVertices, sizeof(triVertices));
        vertexBuffer->unmap();
    }

    // --- Compile shaders ---
    const char* vertexShaderSource = R"(
        struct VSOutput {
            float4 pos : SV_POSITION;
            float3 color : COLOR;
            float2 uv : TEXCOORD;
        };
        VSOutput mainVS(float3 pos : POSITION, float3 color : COLOR, float2 uv : TEXCOORD) {
            VSOutput output;
            output.pos = float4(pos, 1.0);
            output.color = color;
            output.uv = uv;
            return output;
        }
    )";

    const char* pixelShaderSource = R"(
        struct VSOutput {
            float4 pos : SV_POSITION;
            float3 color : COLOR;
            float2 uv : TEXCOORD;
        };
        float4 mainPS(VSOutput input) : SV_TARGET {
            return float4(input.color, 1.0);
        }
    )";

    auto vsBytecode = compile_shader(vertexShaderSource, "mainVS", "vs_5_0");
    auto psBytecode = compile_shader(pixelShaderSource, "mainPS", "ps_5_0");

    if (vsBytecode.empty() || psBytecode.empty()) {
        aether::log::error("Failed to compile shaders");
        return 1;
    }

    // --- Create pipeline ---
    aether::rhi::GfxPipelineDesc pipelineDesc{};
    pipelineDesc.vsBytecode = vsBytecode;
    pipelineDesc.psBytecode = psBytecode;
    pipelineDesc.rtvFormat = aether::rhi::Format::R8G8B8A8_UNORM;
    pipelineDesc.rtvCount = 1;
    pipelineDesc.dsvFormat = aether::rhi::Format::Unknown;

    auto pipeline = g_device->create_graphics_pipeline(pipelineDesc);
    if (!pipeline) {
        aether::log::error("Failed to create pipeline");
        return 1;
    }

    // --- Create swap chain ---
    aether::rhi::SwapChainDesc scDesc{};
    scDesc.windowHandle = g_hwnd;
    scDesc.width = 800;
    scDesc.height = 600;
    scDesc.bufferCount = 3;

    auto swapChain = g_device->create_swap_chain(scDesc);
    if (!swapChain) {
        aether::log::error("Failed to create swap chain");
        return 1;
    }

    aether::log::info("Triangle example initialized successfully!");
    aether::log::info("Entering main loop...");

    // --- Main loop ---
    MSG msg = {};
    uint64_t frameCount = 0;
    while (msg.message != WM_QUIT) {
        // Process window messages
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Get back buffer
        uint32_t backBufferIndex = swapChain->get_current_index();
        auto backBuffer = swapChain->get_back_buffer(backBufferIndex);

        if (backBuffer) {
            // Create command list
            auto cmdList = g_device->create_graphics_command_list();
            cmdList->reset();

            // Transition back buffer to render target state
            // D3D12_RESOURCE_STATE_PRESENT = 0, D3D12_RESOURCE_STATE_RENDER_TARGET = 4
            cmdList->resource_barrier(backBuffer.get(),
                aether::rhi::ResourceState::Common,
                aether::rhi::ResourceState::RenderTarget);

            // Clear (bright red so triangle is visible against it)
            float clearColor[] = {1.0f, 0.0f, 0.0f, 1.0f};
            cmdList->clear_texture(backBuffer.get(), clearColor);
            cmdList->bind_render_targets(backBuffer.get(), nullptr);

            // Set pipeline and draw textured quad
            cmdList->set_viewport(0, 0, 800, 600, 0, 1);
            cmdList->set_scissor(0, 0, 800, 600);
            cmdList->bind_pipeline(pipeline.get());
            cmdList->ia_set_vertex_buffer(0, vertexBuffer.get(), sizeof(Vertex));
            cmdList->draw(3, 1, 0, 0);

            frameCount++;
            if (frameCount % 60 == 0) {
                aether::log::info("Rendered {} frames", frameCount);
            }

            // Transition back to present
            cmdList->resource_barrier(backBuffer.get(),
                aether::rhi::ResourceState::RenderTarget,
                aether::rhi::ResourceState::Common);

            // Execute (execute_command_lists handles close)
            std::unique_ptr<aether::rhi::CommandList> cmdLists[] = {std::move(cmdList)};
            g_device->execute_command_lists(cmdLists);
            swapChain->present(true);
        }
    }

    g_device->wait_for_idle();
    aether::log::info("Triangle example shutting down");
    return 0;
}

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstring>
#include <vector>
#include <memory>
#include <cstdint>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>

import aether.rhi;
import aether.core;
import aether.shaders;

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

// Read shader source from file
std::string read_shader_file(const std::string& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        aether::log::error("Failed to open shader file: {}", path);
        return {};
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
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

    // --- Compile shaders via Aether.Shaders ---
    // Determine shader path relative to executable
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, sizeof(exePath));
    std::string exeDir(exePath);
    auto pos = exeDir.find_last_of('\\');
    if (pos != std::string::npos) {
        exeDir = exeDir.substr(0, pos);
    }
    std::string shaderPath = exeDir + "/../../../../src/Shaders/aether_shaders.slang";

    aether::shaders::ShaderCompiler compiler;

    std::string shaderSource = read_shader_file(shaderPath);
    if (shaderSource.empty()) {
        aether::log::error("Failed to read shader file: {}", shaderPath);
        return 1;
    }

    auto vsResult = compiler.compile({
        .source = shaderSource,
        .entryPoint = "mainVS",
        .type = aether::shaders::ShaderType::Vertex,
        .target = aether::shaders::ShaderTarget::DXIL,
    });
    if (!vsResult.is_valid()) {
        aether::log::error("VS compilation failed: {}", vsResult.get_error());
        return 1;
    }

    auto psResult = compiler.compile({
        .source = shaderSource,
        .entryPoint = "mainPS",
        .type = aether::shaders::ShaderType::Pixel,
        .target = aether::shaders::ShaderTarget::DXIL,
    });
    if (!psResult.is_valid()) {
        aether::log::error("PS compilation failed: {}", psResult.get_error());
        return 1;
    }

    auto vsBytecode = std::vector<std::byte>(
        vsResult.get_bytecode().begin(), vsResult.get_bytecode().end());
    auto psBytecode = std::vector<std::byte>(
        psResult.get_bytecode().begin(), psResult.get_bytecode().end());

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

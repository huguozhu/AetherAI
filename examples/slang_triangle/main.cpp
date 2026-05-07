#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <cstring>
#include <vector>
#include <memory>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
#include <fstream>
#include <sstream>

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
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

HWND create_window(HINSTANCE hInstance, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"AetherSlangTriangleWindow";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"AetherAI - Slang Triangle Demo",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hwnd, nCmdShow);
    return hwnd;
}

// Read shader source from file
std::string read_shader_file(const char* path) {
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
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    // Determine shader path relative to executable
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, sizeof(exePath));
    std::string exeDir(exePath);
    auto pos = exeDir.find_last_of('\\');
    if (pos != std::string::npos) {
        exeDir = exeDir.substr(0, pos);
    }
    std::string shaderPath = exeDir + "/../../../../src/Shaders/aether_shaders.slang";

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

    // --- Compile shaders with Aether.Shaders (Slang) ---
    aether::shaders::ShaderCompiler compiler;

    // Read .slang source
    std::string shaderSource = read_shader_file(shaderPath.c_str());
    if (shaderSource.empty()) {
        // Fallback: embed shader source directly
        aether::log::warn("Could not read shader file, using embedded source");
        shaderSource = R"(
            struct VSOutput {
                float4 pos : SV_POSITION;
                float4 color : COLOR;
            };
            [shader("vertex")]
            VSOutput vertexMain(float3 position : POSITION, float4 color : COLOR) {
                VSOutput o;
                o.pos = float4(position, 1.0);
                o.color = color;
                return o;
            }
            [shader("pixel")]
            float4 pixelMain(VSOutput input) : SV_TARGET {
                return input.color;
            }
        )";
    }

    aether::log::info("Compiling vertex shader with Slang...");
    auto vsResult = compiler.compile({
        .source = shaderSource,
        .entryPoint = "vertexMain",
        .type = aether::shaders::ShaderType::Vertex,
        .target = aether::shaders::ShaderTarget::DXIL,
    });
    if (!vsResult.is_valid()) {
        aether::log::error("Vertex shader compilation failed: {}", vsResult.get_error());
        return 1;
    }

    aether::log::info("Compiling pixel shader with Slang...");
    auto psResult = compiler.compile({
        .source = shaderSource,
        .entryPoint = "pixelMain",
        .type = aether::shaders::ShaderType::Pixel,
        .target = aether::shaders::ShaderTarget::DXIL,
    });
    if (!psResult.is_valid()) {
        aether::log::error("Pixel shader compilation failed: {}", psResult.get_error());
        return 1;
    }

    aether::log::info("Slang shaders compiled successfully! VS: {} bytes, PS: {} bytes",
                       vsResult.get_bytecode().size(), psResult.get_bytecode().size());

    // --- Vertex data (green triangle) ---
    struct Vertex {
        float position[3];
        float color[4];
    };

    Vertex vertices[] = {
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{ 0.0f,  0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    };

    // Create vertex buffer
    aether::rhi::BufferDesc vbDesc{};
    vbDesc.size = sizeof(vertices);
    vbDesc.heap = aether::rhi::HeapType::Upload;
    vbDesc.bindFlags = aether::rhi::BindFlags::VertexBuffer;
    auto vertexBuffer = g_device->create_buffer(vbDesc);
    {
        void* mapped = vertexBuffer->map();
        memcpy(mapped, vertices, sizeof(vertices));
        vertexBuffer->unmap();
    }

    // --- Create pipeline with Slang-compiled shaders ---
    aether::rhi::GfxPipelineDesc pipelineDesc{};
    pipelineDesc.vsBytecode = vsResult.get_bytecode();
    pipelineDesc.psBytecode = psResult.get_bytecode();
    pipelineDesc.rtvFormat = aether::rhi::Format::R8G8B8A8_UNORM;
    pipelineDesc.rtvCount = 1;
    pipelineDesc.dsvFormat = aether::rhi::Format::Unknown;

    auto pipeline = g_device->create_graphics_pipeline(pipelineDesc);
    if (!pipeline) {
        aether::log::error("Failed to create graphics pipeline from Slang shaders");
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

    aether::log::info("Slang triangle example initialized successfully!");
    aether::log::info("Entering main loop...");

    // --- Main loop ---
    MSG msg = {};
    uint64_t frameCount = 0;
    while (msg.message != WM_QUIT) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        uint32_t backBufferIndex = swapChain->get_current_index();
        auto backBuffer = swapChain->get_back_buffer(backBufferIndex);

        if (backBuffer) {
            auto cmdList = g_device->create_graphics_command_list();
            cmdList->reset();

            cmdList->resource_barrier(backBuffer.get(),
                aether::rhi::ResourceState::Common,
                aether::rhi::ResourceState::RenderTarget);

            float clearColor[] = {0.1f, 0.1f, 0.1f, 1.0f};
            cmdList->clear_texture(backBuffer.get(), clearColor);
            cmdList->bind_render_targets(backBuffer.get(), nullptr);

            cmdList->set_viewport(0, 0, 800, 600, 0, 1);
            cmdList->set_scissor(0, 0, 800, 600);
            cmdList->bind_pipeline(pipeline.get());
            cmdList->ia_set_vertex_buffer(0, vertexBuffer.get(), sizeof(Vertex));
            cmdList->draw(3, 1, 0, 0);

            frameCount++;
            if (frameCount % 60 == 0) {
                aether::log::info("Rendered {} frames", frameCount);
            }

            cmdList->resource_barrier(backBuffer.get(),
                aether::rhi::ResourceState::RenderTarget,
                aether::rhi::ResourceState::Common);

            std::unique_ptr<aether::rhi::CommandList> cmdLists[] = {std::move(cmdList)};
            g_device->execute_command_lists(cmdLists);
            swapChain->present(true);
        }
    }

    g_device->wait_for_idle();
    aether::log::info("Slang triangle example shutting down");
    return 0;
}

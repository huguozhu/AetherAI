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
import aether.renderer;

// Global window and device
HWND g_hwnd = nullptr;
std::shared_ptr<aether::rhi::Device> g_device = nullptr;

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

    g_hwnd = create_window(hInstance, SW_SHOW);
    if (!g_hwnd) {
        aether::log::error("Failed to create window");
        return 1;
    }

    g_device = aether::rhi::create_d3d12_device();
    if (!g_device) {
        aether::log::error("Failed to create D3D12 device");
        return 1;
    }

    // --- Create MeshComponent with triangle data ---
    auto mesh = std::make_shared<aether::renderer::MeshComponent>();
    const float triPositions[] = {
        -0.5f, -0.5f, 0.0f,
         0.0f,  0.5f, 0.0f,
         0.5f, -0.5f, 0.0f
    };
    mesh->set_positions(triPositions);

    // Create RenderScene with OctreeSceneManager and register component
    aether::renderer::RenderScene renderScene(g_device);
    renderScene.register_component(mesh);

    aether::log::info("Registered triangle MeshComponent with SceneManager");

    // --- Compile shaders via Aether.Shaders ---
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

    // Use position-only shaders that match MeshComponent's vertex format
    auto vsResult = compiler.compile({
        .source = shaderSource,
        .entryPoint = "forwardVS",
        .type = aether::shaders::ShaderType::Vertex,
        .target = aether::shaders::ShaderTarget::DXIL,
    });
    if (!vsResult.is_valid()) {
        aether::log::error("VS compilation failed: {}", vsResult.get_error());
        return 1;
    }

    auto psResult = compiler.compile({
        .source = shaderSource,
        .entryPoint = "forwardPS",
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

    aether::log::info("Triangle example initialized with MeshComponent + SceneManager!");
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

            float clearColor[] = {1.0f, 0.0f, 0.0f, 1.0f};
            cmdList->clear_texture(backBuffer.get(), clearColor);
            cmdList->bind_render_targets(backBuffer.get(), nullptr);

            // Update SceneManager with identity view (NDC-space rendering)
            aether::renderer::SceneView view{};
            view.viewProj = aether::math::float4x4::identity();
            view.eyePosition = {0, 0, -1};
            view.nearPlane = 0.1f;
            view.farPlane = 100.0f;
            renderScene.update(view);

            // Render via SceneManager — iterates MeshComponents, frustum culls, draws
            cmdList->set_viewport(0, 0, 800, 600, 0, 1);
            cmdList->set_scissor(0, 0, 800, 600);
            renderScene.render_forward(cmdList.get(), pipeline.get());

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
    aether::log::info("Triangle example shutting down");
    return 0;
}

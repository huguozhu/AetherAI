#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstring>
#include <vector>
#include <memory>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>

import aether.rhi;
import aether.core;
import aether.shaders;
import aether.renderer;
import aether.gltf;

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
    const wchar_t CLASS_NAME[] = L"AetherSponzaViewerWindow";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"AetherAI - Sponza Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hwnd, nCmdShow);
    return hwnd;
}

// Embedded shader with view-projection constant buffer
static const char* s_gltfShaderSource = R"(
    struct VSOutput {
        float4 pos : SV_POSITION;
    };

    cbuffer ViewProj : register(b0) {
        float4x4 viewProj;
    };

    [shader("vertex")]
    VSOutput vertexMain(float3 position : POSITION) {
        VSOutput o;
        o.pos = mul(float4(position, 1.0), viewProj);
        return o;
    }

    [shader("pixel")]
    float4 pixelMain(VSOutput input) : SV_TARGET {
        return float4(0.6, 0.5, 0.4, 1.0);
    }
)";

// Try to resolve the Sponza glTF path relative to the executable location
std::string find_sponza_path() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();

    // Try multiple relative paths from exe location to find Sponza asset
    std::vector<std::filesystem::path> candidates = {
        // From build/examples/sponza_viewer/{config}/ -> ../../../../
        exeDir / "../../../../asset/Sponza/Sponza.gltf",
        // From build/{config}/examples/sponza_viewer/ -> ../../../
        exeDir / "../../../asset/Sponza/Sponza.gltf",
        // From build/examples/sponza_viewer/ (no config dir) -> ../../../
        exeDir / "../../../asset/Sponza/Sponza.gltf",
        // Relative to CWD
        std::filesystem::current_path() / "asset/Sponza/Sponza.gltf",
    };

    for (auto& p : candidates) {
        auto normalized = std::filesystem::weakly_canonical(p);
        if (std::filesystem::exists(normalized)) {
            aether::log::info("Found Sponza asset at: {}", normalized.string());
            return normalized.string();
        }
    }

    // Last resort: return the first candidate as error message
    auto first = std::filesystem::weakly_canonical(candidates[0]);
    aether::log::error("Sponza asset not found. Tried paths under: {}", exeDir.string());
    return first.string();
}

int main(int argc, char* argv[]) {
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

    // ── Determine Sponza glTF path ──
    std::string gltfPath;
    if (argc >= 2) {
        gltfPath = argv[1];
        aether::log::info("Using glTF file from command line: {}", gltfPath);
    } else {
        gltfPath = find_sponza_path();
        aether::log::info("Resolved Sponza path: {}", gltfPath);
    }

    // ── Create scene ──
    aether::renderer::RenderScene renderScene(g_device);

    // ── Load Sponza glTF ──
    aether::gltf::GltfImporter importer(g_device);
    auto scene = importer.load_from_file(gltfPath);
    if (!scene.is_valid()) {
        aether::log::error("Failed to load Sponza glTF file: {}", gltfPath);
        return 1;
    }

    aether::log::info("Registering {} meshes, {} cameras, {} lights with scene",
                      scene.meshes.size(), scene.cameras.size(), scene.lights.size());

    for (auto& mesh : scene.meshes) {
        renderScene.register_component(std::move(mesh));
    }
    for (auto& cam : scene.cameras) {
        renderScene.register_component(std::move(cam));
    }
    for (auto& light : scene.lights) {
        renderScene.register_component(std::move(light));
    }

    // ── Set up camera ──
    auto activeCamera = std::make_shared<aether::renderer::CameraComponent>();
    activeCamera->position = {0.0f, 1.5f, 6.0f};
    activeCamera->target = {0.0f, 1.0f, 0.0f};
    activeCamera->fov = 60.0f;
    activeCamera->aspect = 1280.0f / 720.0f;
    activeCamera->nearPlane = 0.1f;
    activeCamera->farPlane = 500.0f;

    // Try to find a camera from the scene (Sponza may have embedded cameras)
    for (uint32_t i = 0; i < 100; ++i) {
        auto comp = renderScene.get_component(i);
        if (comp) {
            if (auto* cam = dynamic_cast<aether::renderer::CameraComponent*>(comp.get())) {
                activeCamera = std::make_shared<aether::renderer::CameraComponent>(*cam);
                activeCamera->aspect = 1280.0f / 720.0f;
                activeCamera->farPlane = 500.0f;
                aether::log::info("Using camera from glTF scene (pos: {:.2f},{:.2f},{:.2f})",
                                  activeCamera->position.x,
                                  activeCamera->position.y,
                                  activeCamera->position.z);
                break;
            }
        }
    }

    // ── Compile shaders ──
    aether::shaders::ShaderCompiler compiler;

    aether::log::info("Compiling vertex shader...");
    auto vsResult = compiler.compile({
        .source = s_gltfShaderSource,
        .entryPoint = "vertexMain",
        .type = aether::shaders::ShaderType::Vertex,
        .target = aether::shaders::ShaderTarget::DXIL,
    });
    if (!vsResult.is_valid()) {
        aether::log::error("Vertex shader compilation failed: {}", vsResult.get_error());
        return 1;
    }

    aether::log::info("Compiling pixel shader...");
    auto psResult = compiler.compile({
        .source = s_gltfShaderSource,
        .entryPoint = "pixelMain",
        .type = aether::shaders::ShaderType::Pixel,
        .target = aether::shaders::ShaderTarget::DXIL,
    });
    if (!psResult.is_valid()) {
        aether::log::error("Pixel shader compilation failed: {}", psResult.get_error());
        return 1;
    }

    aether::log::info("Shaders compiled successfully! VS: {} bytes, PS: {} bytes",
                      vsResult.get_bytecode().size(), psResult.get_bytecode().size());

    // ── Create pipeline ──
    aether::rhi::GfxPipelineDesc pipelineDesc{};
    pipelineDesc.vsBytecode = vsResult.get_bytecode();
    pipelineDesc.psBytecode = psResult.get_bytecode();
    pipelineDesc.rtvFormat = aether::rhi::Format::R8G8B8A8_UNORM;
    pipelineDesc.rtvCount = 1;
    pipelineDesc.dsvFormat = aether::rhi::Format::Unknown;
    pipelineDesc.frontCounterClockwise = true; // glTF uses CCW winding

    auto pipeline = g_device->create_graphics_pipeline(pipelineDesc);
    if (!pipeline) {
        aether::log::error("Failed to create graphics pipeline");
        return 1;
    }

    // ── Create constant buffer for view-projection matrix ──
    auto cbDesc = aether::rhi::BufferDesc{
        .size = 256,
        .heap = aether::rhi::HeapType::Upload,
        .bindFlags = aether::rhi::BindFlags::ConstantBuffer,
    };
    auto constantBuffer = g_device->create_buffer(cbDesc);
    if (!constantBuffer) {
        aether::log::error("Failed to create constant buffer");
        return 1;
    }

    aether::rhi::BindingLayout bindingLayout{};
    bindingLayout.numDescriptors = 14;
    auto shaderBinding = g_device->create_shader_binding(bindingLayout);
    if (shaderBinding) {
        shaderBinding->set_buffer(0, constantBuffer);
    }

    // ── Create swap chain ──
    aether::rhi::SwapChainDesc scDesc{};
    scDesc.windowHandle = g_hwnd;
    scDesc.width = 1280;
    scDesc.height = 720;
    scDesc.bufferCount = 3;

    auto swapChain = g_device->create_swap_chain(scDesc);
    if (!swapChain) {
        aether::log::error("Failed to create swap chain");
        return 1;
    }

    aether::log::info("Sponza viewer initialized!");
    aether::log::info("Entering main loop...");

    // ── Main loop ──
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

            float clearColor[] = {0.15f, 0.15f, 0.18f, 1.0f};
            cmdList->clear_texture(backBuffer.get(), clearColor);
            cmdList->bind_render_targets(backBuffer.get(), nullptr);

            cmdList->set_viewport(0, 0, 1280, 720, 0, 1);
            cmdList->set_scissor(0, 0, 1280, 720);

            // Update scene view from camera
            auto sceneView = activeCamera->get_scene_view();
            renderScene.update(sceneView);

            // Update constant buffer with view-projection matrix
            if (constantBuffer) {
                void* mapped = constantBuffer->map();
                if (mapped) {
                    auto viewMatrix = activeCamera->get_view_matrix();
                    auto projMatrix = activeCamera->get_projection_matrix();

                    aether::math::float4x4 viewProj{};
                    for (int row = 0; row < 4; ++row) {
                        for (int col = 0; col < 4; ++col) {
                            float sum = 0;
                            for (int k = 0; k < 4; ++k)
                                sum += viewMatrix.m[row][k] * projMatrix.m[k][col];
                            viewProj.m[row][col] = sum;
                        }
                    }
                    std::memcpy(mapped, &viewProj, sizeof(aether::math::float4x4));
                    constantBuffer->unmap();
                }
            }

            // Render: bind pipeline + binding + iterate meshes
            cmdList->bind_pipeline(pipeline.get());

            if (shaderBinding) {
                cmdList->bind_descriptor(0, shaderBinding.get());
            }

            // Manual mesh iteration
            for (uint32_t i = 0; i < 2000; ++i) {
                auto comp = renderScene.get_component(i);
                if (!comp) continue;

                auto* mesh = dynamic_cast<aether::renderer::MeshComponent*>(comp.get());
                if (!mesh || !mesh->has_gpu_resources()) continue;

                auto* vb = mesh->vertex_buffer();
                if (vb) {
                    constexpr uint32_t kPositionStride = sizeof(float) * 3;
                    cmdList->ia_set_vertex_buffer(0, vb, kPositionStride);
                    if (mesh->index_count() > 0) {
                        cmdList->ia_set_index_buffer(mesh->index_buffer());
                        cmdList->draw_indexed(mesh->index_count(), 1, 0, 0, 0);
                    } else {
                        cmdList->draw(mesh->vertex_count(), 1, 0, 0);
                    }
                }
            }

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
    aether::log::info("Sponza viewer shutting down");
    return 0;
}

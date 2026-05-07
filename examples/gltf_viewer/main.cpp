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
    const wchar_t CLASS_NAME[] = L"AetherGltfViewerWindow";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"AetherAI - glTF Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
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
        return float4(0.2, 0.7, 0.3, 1.0);
    }
)";

// Simple fallback triangle shader (no projection, clip-space positions)
static const char* s_triangleShaderSource = R"(
    struct VSOutput {
        float4 pos : SV_POSITION;
    };
    [shader("vertex")]
    VSOutput vertexMain(float3 position : POSITION) {
        VSOutput o;
        o.pos = float4(position, 1.0);
        return o;
    }
    [shader("pixel")]
    float4 pixelMain(VSOutput input) : SV_TARGET {
        return float4(0.0, 1.0, 0.0, 1.0);
    }
)";

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

    // Determine glTF file path from command line args
    std::string gltfPath;
    bool useEmbeddedTriangle = true;
    if (argc >= 2) {
        gltfPath = argv[1];
        useEmbeddedTriangle = false;
        aether::log::info("Loading glTF file: {}", gltfPath);
    } else {
        aether::log::info("No glTF file specified, using embedded triangle");
    }

    // ── Create scene ──
    aether::renderer::RenderScene renderScene(g_device);

    // ── Load glTF or fallback triangle ──
    if (!useEmbeddedTriangle) {
        aether::gltf::GltfImporter importer(g_device);
        auto scene = importer.load_from_file(gltfPath);
        if (!scene.is_valid()) {
            aether::log::error("Failed to load glTF file: {}", gltfPath);
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
    } else {
        // Fallback: embedded triangle
        auto mesh = std::make_shared<aether::renderer::MeshComponent>();
        const float triPositions[] = {
            -0.5f, -0.5f, 0.0f,
             0.0f,  0.5f, 0.0f,
             0.5f, -0.5f, 0.0f
        };
        mesh->set_positions(triPositions);
        renderScene.register_component(mesh);

        auto camera = std::make_shared<aether::renderer::CameraComponent>();
        camera->position = {0, 0, -2};
        camera->target = {0, 0, 0};
        camera->fov = 60.0f;
        camera->aspect = 800.0f / 600.0f;
        renderScene.register_component(camera);
    }

    // ── Get the first camera for scene updates ──
    // If no camera in scene, create a default one
    auto activeCamera = std::make_shared<aether::renderer::CameraComponent>();
    activeCamera->aspect = 800.0f / 600.0f;

    // Try to find a camera from the scene
    if (!useEmbeddedTriangle) {
        // Find first CameraComponent registered
        for (uint32_t i = 0; i < 100; ++i) {
            auto comp = renderScene.get_component(i);
            if (comp) {
                if (auto* cam = dynamic_cast<aether::renderer::CameraComponent*>(comp.get())) {
                    activeCamera = std::make_shared<aether::renderer::CameraComponent>(*cam);
                    activeCamera->aspect = 800.0f / 600.0f; // override aspect to match window
                    aether::log::info("Using camera from glTF scene (pos: {:.2f},{:.2f},{:.2f})",
                                      activeCamera->position.x,
                                      activeCamera->position.y,
                                      activeCamera->position.z);
                    break;
                }
            }
        }
    }

    // ── Compile shaders ──
    aether::shaders::ShaderCompiler compiler;

    const char* shaderSrc = useEmbeddedTriangle ? s_triangleShaderSource : s_gltfShaderSource;

    aether::log::info("Compiling vertex shader...");
    auto vsResult = compiler.compile({
        .source = shaderSrc,
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
        .source = shaderSrc,
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
    pipelineDesc.frontCounterClockwise = !useEmbeddedTriangle; // glTF uses CCW winding

    auto pipeline = g_device->create_graphics_pipeline(pipelineDesc);
    if (!pipeline) {
        aether::log::error("Failed to create graphics pipeline");
        return 1;
    }

    // ── Create constant buffer for view-projection matrix (glTF mode only) ──
    std::shared_ptr<aether::rhi::Buffer> constantBuffer;
    std::shared_ptr<aether::rhi::ShaderBinding> shaderBinding;

    if (!useEmbeddedTriangle) {
        auto cbDesc = aether::rhi::BufferDesc{
            .size = 256, // 16 floats, aligned
            .heap = aether::rhi::HeapType::Upload,
            .bindFlags = aether::rhi::BindFlags::ConstantBuffer,
        };
        constantBuffer = g_device->create_buffer(cbDesc);
        if (!constantBuffer) {
            aether::log::error("Failed to create constant buffer");
            return 1;
        }

        aether::rhi::BindingLayout bindingLayout{};
        bindingLayout.numDescriptors = 14; // CBV b0-b13
        shaderBinding = g_device->create_shader_binding(bindingLayout);
        if (shaderBinding) {
            shaderBinding->set_buffer(0, constantBuffer); // CBV b0: view-projection
        }
    }

    // ── Create swap chain ──
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

    aether::log::info("glTF viewer initialized!");
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

            float clearColor[] = {0.1f, 0.1f, 0.1f, 1.0f};
            cmdList->clear_texture(backBuffer.get(), clearColor);
            cmdList->bind_render_targets(backBuffer.get(), nullptr);

            cmdList->set_viewport(0, 0, 800, 600, 0, 1);
            cmdList->set_scissor(0, 0, 800, 600);

            // Update scene view from camera
            auto sceneView = activeCamera->get_scene_view();
            renderScene.update(sceneView);

            // Update constant buffer with view-projection matrix (glTF mode)
            if (!useEmbeddedTriangle && constantBuffer) {
                void* mapped = constantBuffer->map();
                if (mapped) {
                    // viewProj = projection * view
                    auto viewMatrix = activeCamera->get_view_matrix();
                    auto projMatrix = activeCamera->get_projection_matrix();

                    // Compute viewProj = view * proj (row-major) or proj * view (col-major)
                    // Our math uses row-major vectors, so: result = view * proj
                    // But mul(vector, matrix) in HLSL assumes row-major, so we need viewProj = view * proj
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

            if (!useEmbeddedTriangle && shaderBinding) {
                cmdList->bind_descriptor(0, shaderBinding.get());
            }

            // Manual mesh iteration (render_forward doesn't support custom bindings)
            for (uint32_t i = 0; i < 500; ++i) {
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
    aether::log::info("glTF viewer shutting down");
    return 0;
}

module;
#include <vector>
#include <cstdint>
#include <cstddef>
#ifdef _WIN32
#include <windows.h>
#include <d3dcompiler.h>
#endif

module aether.renderer;

import aether.core;
import aether.rhi;

namespace aether::renderer {

// Embedded HLSL compute shader for frustum culling + LOD selection
static const char* cullingShaderSrc = R"(
struct SceneObject {
    float4x4 worldMatrix;
    float4   boundingSphere; // xyz=center, w=radius
    uint     meshIndex;
    uint     lodCount;
    float    lodDistances[4];
    uint2    _pad;
};

struct VisibleInfo {
    uint visible : 1;
    uint lodLevel : 3;
    uint _pad : 28;
};

struct ViewData {
    float4x4 viewProj;
    float3   eyePos;
    float    nearPlane;
    float    farPlane;
};

ConstantBuffer<ViewData> viewData : register(b0);
StructuredBuffer<SceneObject> sceneObjects : register(t0);
RWStructuredBuffer<VisibleInfo> visibleOutput : register(u0);

[numthreads(64, 1, 1)]
void mainCS(uint3 dispatchId : SV_DispatchThreadID) {
    uint objIndex = dispatchId.x;
    uint objectCount;
    uint stride;
    sceneObjects.GetDimensions(objectCount, stride);
    if (objIndex >= objectCount) return;

    SceneObject obj = sceneObjects[objIndex];
    VisibleInfo info = (VisibleInfo)0;

    // Frustum culling: check sphere against all 6 planes of view-proj
    float4 sphere = obj.boundingSphere;
    float4 c0 = viewData.viewProj[0];
    float4 c1 = viewData.viewProj[1];
    float4 c2 = viewData.viewProj[2];
    float4 c3 = viewData.viewProj[3];

    // Left: m3 + m0
    float d = c3.x + c0.x;
    if (d < -sphere.w * abs(c3.w + c0.w)) { visibleOutput[objIndex] = info; return; }

    // Right: m3 - m0
    d = c3.x - c0.x;
    if (d < -sphere.w * abs(c3.w - c0.w)) { visibleOutput[objIndex] = info; return; }

    // Bottom: m3 + m1
    d = c3.y + c0.y;
    if (d < -sphere.w * abs(c3.w + c0.w)) { visibleOutput[objIndex] = info; return; }

    // Top: m3 - m1
    d = c3.y - c0.y;
    if (d < -sphere.w * abs(c3.w - c0.w)) { visibleOutput[objIndex] = info; return; }

    // Near: m3 + m2
    d = c3.z + c0.z;
    if (d < -sphere.w * abs(c3.w + c0.w)) { visibleOutput[objIndex] = info; return; }

    // Far: m3 - m2
    d = c3.z - c0.z;
    if (d < -sphere.w * abs(c3.w - c0.w)) { visibleOutput[objIndex] = info; return; }

    // Sphere is visible — determine LOD
    float3 eyeToObj = sphere.xyz - viewData.eyePos;
    float dist = length(eyeToObj);

    uint lod = 0;
    for (uint i = 0; i < obj.lodCount && i < 4; ++i) {
        if (dist > obj.lodDistances[i]) lod = i + 1;
    }
    if (lod >= obj.lodCount) lod = obj.lodCount - 1;

    info.visible = 1;
    info.lodLevel = lod;
    visibleOutput[objIndex] = info;
}
)";

CullingJob::CullingJob(std::shared_ptr<rhi::Device> device,
                       std::shared_ptr<rhi::Buffer> sceneBuffer,
                       std::shared_ptr<rhi::Buffer> visibleBuffer,
                       uint32_t maxObjects)
    : m_device(std::move(device))
    , m_sceneBuffer(std::move(sceneBuffer))
    , m_visibleBuffer(std::move(visibleBuffer))
    , m_maxObjects(maxObjects)
{
    // Create constant buffer for view data
    auto cbDesc = rhi::BufferDesc{
        .size = sizeof(SceneView),
        .heap = rhi::HeapType::Upload,
        .bindFlags = rhi::BindFlags::ConstantBuffer,
    };
    m_constantBuffer = m_device->create_buffer(cbDesc);

    // Create shader binding: 14 CBV slots + 1 SRV at slot 14
    rhi::BindingLayout layout{};
    layout.numDescriptors = 15;
    m_binding = m_device->create_shader_binding(layout);
    if (m_binding) {
        // Slot 0: view data constant buffer
        m_binding->set_buffer(0, m_constantBuffer);
    }

    // Compile culling compute shader
    // In production, use pre-compiled shaders from Aether.Shaders
    auto compileCS = [](const char* source, const char* entry) -> std::vector<std::byte> {
#ifdef _WIN32
        ID3DBlob* blob = nullptr;
        ID3DBlob* error = nullptr;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
                                entry, "cs_5_0", flags, 0, &blob, &error);
        if (FAILED(hr)) {
            if (error) {
                log::error("CullingJob: shader compile error: {}",
                           static_cast<const char*>(error->GetBufferPointer()));
                error->Release();
            }
            return {};
        }
        std::vector<std::byte> result(blob->GetBufferSize());
        memcpy(result.data(), blob->GetBufferPointer(), blob->GetBufferSize());
        blob->Release();
        return result;
#else
        (void)source; (void)entry;
        return {};
#endif
    };

    auto csBytecode = compileCS(cullingShaderSrc, "mainCS");
    if (csBytecode.empty()) {
        log::error("CullingJob: failed to compile culling shader");
        return;
    }

    rhi::ComputePipelineDesc pipelineDesc{};
    pipelineDesc.csBytecode = csBytecode;
    m_pipeline = m_device->create_compute_pipeline(pipelineDesc);
    if (!m_pipeline) {
        log::error("CullingJob: failed to create culling pipeline");
        return;
    }

    m_shadersLoaded = true;
    log::info("CullingJob: initialized ({} objects)", m_maxObjects);
}

void CullingJob::update_view(const SceneView& view) {
    if (!m_constantBuffer) return;

    void* mapped = m_constantBuffer->map();
    if (mapped) {
        memcpy(mapped, &view, sizeof(SceneView));
        m_constantBuffer->unmap();
    }
}

void CullingJob::bind(rhi::ComputeCommandList* cmdList) {
    if (!cmdList || !m_shadersLoaded) return;

    cmdList->bind_pipeline(m_pipeline.get());
    cmdList->bind_descriptor(0, m_binding.get());
}

void CullingJob::dispatch(rhi::ComputeCommandList* cmdList) {
    if (!cmdList || !m_shadersLoaded) return;

    bind(cmdList);

    // Dispatch with one thread group per 64 objects
    uint32_t groupsX = (m_maxObjects + 63) / 64;
    cmdList->dispatch(groupsX, 1, 1);

    log::debug("CullingJob: dispatched {} groups for {} objects", groupsX, m_maxObjects);
}

} // namespace aether::renderer

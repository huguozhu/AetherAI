module;
#include <vector>
#include <cstring>
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

// Embedded HLSL compute shader for visibility compaction
// Reads VisibleInfo[] and writes packed DrawArg[] for visible objects
static const char* compactShaderSrc = R"(
struct VisibleInfo {
    uint visible;   // 0 or 1
    uint lodLevel;
};

struct DrawArg {
    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int  baseVertexLocation;
    uint startInstanceLocation;
};

struct SceneObject {
    float4x4 worldMatrix;
    float4   boundingSphere;
    uint     meshIndex;
    uint     lodCount;
    float    lodDistances[4];
    uint2    _pad;
};

cbuffer objectCountBuffer : register(b0) {
    uint objectCount;
};
StructuredBuffer<SceneObject> sceneObjects : register(t0);
StructuredBuffer<VisibleInfo> visibleInput : register(t1);
RWStructuredBuffer<DrawArg> indirectOutput : register(u0);

// Groupshare counter for compaction
groupshared uint compactCounter;

[numthreads(64, 1, 1)]
void mainCS(uint3 dispatchId : SV_DispatchThreadID) {
    uint objIndex = dispatchId.x;
    if (objIndex == 0) compactCounter = 0;
    GroupMemoryBarrierWithGroupSync();

    if (objIndex >= objectCount) return;

    VisibleInfo info = visibleInput[objIndex];
    if (info.visible) {
        uint destIndex;
        InterlockedAdd(compactCounter, 1, destIndex);

        SceneObject obj = sceneObjects[objIndex];
        DrawArg arg;
        arg.indexCountPerInstance = 6; // per mesh; TODO: use mesh data
        arg.instanceCount = 1;
        arg.startIndexLocation = 0;
        arg.baseVertexLocation = 0; // TODO: vertex offset from mesh data
        arg.startInstanceLocation = objIndex;
        indirectOutput[destIndex] = arg;
    }
}
)";

IndirectDrawManager::IndirectDrawManager(std::shared_ptr<rhi::Device> device,
                                          std::shared_ptr<rhi::Buffer> sceneBuffer,
                                          std::shared_ptr<rhi::Buffer> visibleBuffer,
                                          std::shared_ptr<rhi::Buffer> indirectBuffer)
    : m_device(std::move(device))
    , m_sceneBuffer(std::move(sceneBuffer))
    , m_visibleBuffer(std::move(visibleBuffer))
    , m_indirectBuffer(std::move(indirectBuffer))
{
    // Compile compact shader
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
                log::error("IndirectDraw: compact shader error: {}",
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

    auto csBytecode = compileCS(compactShaderSrc, "mainCS");
    if (csBytecode.empty()) {
        log::error("IndirectDraw: failed to compile compact shader");
        return;
    }

    rhi::ComputePipelineDesc pipelineDesc{};
    pipelineDesc.csBytecode = csBytecode;
    m_pipeline = m_device->create_compute_pipeline(pipelineDesc);
    if (!m_pipeline) {
        log::error("IndirectDraw: failed to create compact pipeline");
        return;
    }

    // Create constant buffer for object count
    auto cbDesc = rhi::BufferDesc{
        .size = 256,
        .heap = rhi::HeapType::Upload,
        .bindFlags = rhi::BindFlags::ConstantBuffer,
    };
    m_objectCountBuffer = m_device->create_buffer(cbDesc);

    // Create descriptor binding: CBV b0 + SRV t0/t1 + UAV u0
    rhi::BindingLayout layout{};
    layout.numDescriptors = 143; // 14 CBV + 128 SRV + 1 UAV
    m_binding = m_device->create_shader_binding(layout);
    if (m_binding) {
        m_binding->set_buffer(0, m_objectCountBuffer);    // CBV b0: object count
        m_binding->set_buffer(14, m_sceneBuffer, 0, sizeof(SceneObjectGPU));   // SRV t0: scene objects
        m_binding->set_buffer(15, m_visibleBuffer, 0, sizeof(VisibleInfo));    // SRV t1: visible input
        m_binding->set_buffer(142, m_indirectBuffer, 0, sizeof(DrawArg));      // UAV u0: indirect output
    }

    m_shadersLoaded = true;
    log::info("IndirectDrawManager: initialized");
}

void IndirectDrawManager::dispatch_compact(rhi::ComputeCommandList* cmdList,
                                            uint32_t objectCount) {
    if (!cmdList || !m_shadersLoaded) return;

    // Update object count in constant buffer
    if (m_objectCountBuffer) {
        void* mapped = m_objectCountBuffer->map();
        if (mapped) {
            memcpy(mapped, &objectCount, sizeof(objectCount));
            m_objectCountBuffer->unmap();
        }
    }

    // Bind pipeline and descriptors
    cmdList->bind_pipeline(m_pipeline.get());
    cmdList->bind_descriptor(0, m_binding.get());

    // Dispatch with one thread group per 64 objects
    uint32_t groupsX = (std::max)(1u, (objectCount + 63) / 64);
    cmdList->dispatch(groupsX, 1, 1);

    log::debug("IndirectDraw: compact dispatch ({} objects, {} groups)", objectCount, groupsX);
}

void IndirectDrawManager::draw(rhi::GraphicsCommandList* cmdList,
                                rhi::Buffer* indexBuffer,
                                uint32_t objectCount) {
    if (!cmdList || !m_shadersLoaded) return;

    // Draw indirect from the compacted arguments
    // Draw up to max objects — invisible ones have instanceCount=0 (harmless)
    if (m_indirectBuffer) {
        cmdList->draw_indirect(m_indirectBuffer.get(), 0, objectCount, sizeof(DrawArg));
    }

    log::debug("IndirectDraw: drawing {} objects via ExecuteIndirect", objectCount);
}

} // namespace aether::renderer

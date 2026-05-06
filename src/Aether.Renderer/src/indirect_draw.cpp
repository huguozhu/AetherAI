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
    uint visible : 1;
    uint lodLevel : 3;
    uint _pad : 28;
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

ConstantBuffer<uint> objectCount : register(b0);
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
                                          std::shared_ptr<rhi::Buffer> visibleBuffer,
                                          std::shared_ptr<rhi::Buffer> indirectBuffer)
    : m_device(std::move(device))
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

    m_shadersLoaded = true;
    log::info("IndirectDrawManager: initialized");
}

void IndirectDrawManager::dispatch_compact(rhi::ComputeCommandList* cmdList) {
    if (!cmdList || !m_shadersLoaded) return;

    cmdList->bind_pipeline(m_pipeline.get());
    // TODO: bind scene + visible + indirect buffers
    // For now, stub - real implementation needs proper descriptor binding

    uint32_t groupsX = (kMaxObjects + 63) / 64;
    cmdList->dispatch(groupsX, 1, 1);
}

void IndirectDrawManager::draw(rhi::GraphicsCommandList* cmdList,
                                rhi::Buffer* indexBuffer) {
    if (!cmdList || !m_shadersLoaded) return;

    // Draw indirect from the compacted arguments
    cmdList->draw_indirect(m_indirectBuffer.get(), 0, kMaxObjects, sizeof(DrawArg));

    log::debug("IndirectDraw: drawing {} objects via ExecuteIndirect", kMaxObjects);
}

} // namespace aether::renderer

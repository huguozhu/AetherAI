module;
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <cstdint>
#include <cstddef>

export module aether.renderer;

import <coroutine>;

import aether.core;
import aether.rhi;
import aether.resources;

export namespace aether::renderer {

// === Constants ===
inline constexpr uint32_t kMaxObjects = 10000;
inline constexpr uint32_t kMaxLODs = 4;
inline constexpr uint32_t kDescriptorTableSize = 15; // 14 CBV + 1 SRV

// === GPU Data Structures (mirrors HLSL layout) ===

// Per-object data uploaded to GPU
struct alignas(256) SceneObjectGPU {
    aether::math::float4x4 worldMatrix;
    aether::math::float4   boundingSphere; // xyz=center, w=radius
    uint32_t               meshIndex;
    uint32_t               lodCount;
    float                  lodDistances[4];
    uint32_t               _pad[2];
};

// Compact draw arguments output by culling
struct alignas(16) DrawArg {
    uint32_t indexCountPerInstance;
    uint32_t instanceCount;
    uint32_t startIndexLocation;
    int32_t  baseVertexLocation;
    uint32_t startInstanceLocation;
};

// Culling output per object
struct alignas(16) VisibleInfo {
    uint32_t visible : 1;
    uint32_t lodLevel : 3;
    uint32_t _pad : 28;
};

// === SceneView (per-frame camera data) ===
struct SceneView {
    aether::math::float4x4 viewProj;
    aether::math::float3   eyePosition;
    float                  nearPlane;
    float                  farPlane;
};

// === GPUSceneManager ===
class GPUSceneManager {
public:
    explicit GPUSceneManager(std::shared_ptr<rhi::Device> device);

    // Add/update objects
    uint32_t add_object(const SceneObjectGPU& obj);
    void     update_object(uint32_t index, const SceneObjectGPU& obj);
    void     remove_object(uint32_t index);

    // Upload scene data to GPU
    void upload_scene_data(rhi::CopyCommandList* copyCmd);

    // Get GPU buffers for rendering
    rhi::Buffer* scene_buffer() const { return m_sceneBuffer.get(); }
    rhi::Buffer* visible_buffer() const { return m_visibleBuffer.get(); }
    rhi::Buffer* indirect_buffer() const { return m_indirectBuffer.get(); }
    std::shared_ptr<rhi::Buffer> scene_buffer_shared() const { return m_sceneBuffer; }
    std::shared_ptr<rhi::Buffer> visible_buffer_shared() const { return m_visibleBuffer; }
    std::shared_ptr<rhi::Buffer> indirect_buffer_shared() const { return m_indirectBuffer; }

    uint32_t object_count() const { return m_objectCount; }

private:
    std::shared_ptr<rhi::Device> m_device;
    rhi::BufferPtr m_sceneBuffer;    // SceneObjectGPU[kMaxObjects]
    rhi::BufferPtr m_visibleBuffer;  // VisibleInfo[kMaxObjects]
    rhi::BufferPtr m_indirectBuffer; // DrawArg[kMaxObjects]

    std::vector<SceneObjectGPU> m_objects;
    uint32_t m_objectCount = 0;
    bool m_dirty = false;
};

// === CullingJob ===
class CullingJob {
public:
    CullingJob(std::shared_ptr<rhi::Device> device,
               std::shared_ptr<rhi::Buffer> sceneBuffer,
               std::shared_ptr<rhi::Buffer> visibleBuffer,
               uint32_t maxObjects);

    // Update per-frame data and dispatch culling
    void update_view(const SceneView& view);
    void dispatch(rhi::ComputeCommandList* cmdList);
    void bind(rhi::ComputeCommandList* cmdList);

private:
    std::shared_ptr<rhi::Device> m_device;
    rhi::BufferPtr m_constantBuffer;  // SceneView uploaded per frame
    rhi::ShaderBindingPtr m_binding;

    rhi::BufferPtr m_sceneBuffer;
    rhi::BufferPtr m_visibleBuffer;
    uint32_t m_maxObjects;

    rhi::ComputePipelinePtr m_pipeline;
    bool m_shadersLoaded = false;
};

// === IndirectDrawManager ===
class IndirectDrawManager {
public:
    IndirectDrawManager(std::shared_ptr<rhi::Device> device,
                        std::shared_ptr<rhi::Buffer> visibleBuffer,
                        std::shared_ptr<rhi::Buffer> indirectBuffer);

    void dispatch_compact(rhi::ComputeCommandList* cmdList);
    void draw(rhi::GraphicsCommandList* cmdList, rhi::Buffer* indexBuffer);

private:
    std::shared_ptr<rhi::Device> m_device;
    rhi::BufferPtr m_visibleBuffer;
    rhi::BufferPtr m_indirectBuffer;
    rhi::ShaderBindingPtr m_binding;
    rhi::ComputePipelinePtr m_pipeline;
    bool m_shadersLoaded = false;
};

// === High-level RenderScene ===
class RenderScene {
public:
    explicit RenderScene(std::shared_ptr<rhi::Device> device);

    // Add procedurally generated objects
    uint32_t add_object(const SceneObjectGPU& obj);

    // Per-frame update
    void update(const SceneView& view);

    // Render a frame
    void render(rhi::GraphicsCommandList* gfxCmd,
                rhi::ComputeCommandList*  computeCmd,
                rhi::CopyCommandList*     copyCmd);

private:
    std::shared_ptr<rhi::Device> m_device;
    std::unique_ptr<GPUSceneManager> m_sceneManager;
    std::unique_ptr<CullingJob> m_cullingJob;
    std::unique_ptr<IndirectDrawManager> m_indirectDraw;
};

} // namespace aether::renderer

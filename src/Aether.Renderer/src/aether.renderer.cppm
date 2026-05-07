module;
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <cstdint>
#include <cstddef>
#include <span>

export module aether.renderer;

import <coroutine>;

import aether.core;
import aether.rhi;
import aether.resources;

export namespace aether::renderer {

// === Constants ===
inline constexpr uint32_t kMaxObjects = 10000;
inline constexpr uint32_t kMaxLODs = 4;

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
                        std::shared_ptr<rhi::Buffer> sceneBuffer,
                        std::shared_ptr<rhi::Buffer> visibleBuffer,
                        std::shared_ptr<rhi::Buffer> indirectBuffer);

    void dispatch_compact(rhi::ComputeCommandList* cmdList, uint32_t objectCount);
    void draw(rhi::GraphicsCommandList* cmdList, rhi::Buffer* indexBuffer, uint32_t objectCount);

private:
    std::shared_ptr<rhi::Device> m_device;
    rhi::BufferPtr m_sceneBuffer;
    rhi::BufferPtr m_visibleBuffer;
    rhi::BufferPtr m_indirectBuffer;
    rhi::BufferPtr m_objectCountBuffer;
    rhi::ShaderBindingPtr m_binding;
    rhi::ComputePipelinePtr m_pipeline;
    bool m_shadersLoaded = false;
};

// === Component System ===

enum class LightType : uint8_t { Directional, Point, Spot };

class Component {
public:
    virtual ~Component() = default;
    virtual const char* type_name() const = 0;
};

class MeshComponent : public Component {
public:
    MeshComponent() = default;

    const char* type_name() const override { return "MeshComponent"; }

    // Mesh data
    void set_positions(std::span<const float> positions);
    void set_normals(std::span<const float> normals);
    void set_uvs(std::span<const float> uvs);
    void set_indices(std::span<const uint32_t> indices);

    // Transform
    void set_world_matrix(const math::float4x4& matrix);
    const math::float4x4& world_matrix() const { return m_worldMatrix; }

    void set_bounding_sphere(const math::BoundingSphere& sphere);
    const math::BoundingSphere& bounding_sphere() const { return m_boundingSphere; }

    // LOD
    void add_lod(std::span<const float> positions,
                 std::span<const uint32_t> indices,
                 float distanceThreshold);

    // GPU resource creation + upload
    void create_gpu_resources(rhi::Device* device);
    void upload(rhi::CopyCommandList* copyCmd);
    bool has_gpu_resources() const { return m_vertexBuffer != nullptr; }

    rhi::Buffer* vertex_buffer() const { return m_vertexBuffer.get(); }
    rhi::Buffer* index_buffer() const { return m_indexBuffer.get(); }
    uint32_t vertex_count() const { return m_vertexCount; }
    uint32_t index_count() const { return m_indexCount; }

    // Convert to GPU scene object (for GPU-driven pipeline)
    SceneObjectGPU to_scene_object(uint32_t meshIndex) const;

    // Raw data access
    const std::vector<float>& positions() const { return m_positions; }
    const std::vector<uint32_t>& indices() const { return m_indices; }

private:
    std::vector<float> m_positions;
    std::vector<float> m_normals;
    std::vector<float> m_uvs;
    std::vector<uint32_t> m_indices;

    math::float4x4 m_worldMatrix = math::float4x4::identity();
    math::BoundingSphere m_boundingSphere{{0, 0, 0}, 1.0f};

    rhi::BufferPtr m_vertexBuffer;
    rhi::BufferPtr m_indexBuffer;
    uint32_t m_vertexCount = 0;
    uint32_t m_indexCount = 0;

    struct LODLevel {
        std::vector<float> positions;
        std::vector<uint32_t> indices;
        float distanceThreshold = 0.0f;
    };
    std::vector<LODLevel> m_lods;
};

class LightComponent : public Component {
public:
    LightType type = LightType::Point;
    math::float3 color{1, 1, 1};
    float intensity = 1.0f;
    float range = 10.0f;
    float spotAngle = 45.0f;
    math::float3 position{0, 0, 0};
    math::float3 direction{0, -1, 0};

    const char* type_name() const override { return "LightComponent"; }
};

class CameraComponent : public Component {
public:
    float fov = 60.0f;
    float aspect = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    math::float3 position{0, 0, 0};
    math::float3 target{0, 0, -1};
    math::float3 up{0, 1, 0};

    math::float4x4 get_view_matrix() const;
    math::float4x4 get_projection_matrix() const;
    SceneView get_scene_view() const;

    const char* type_name() const override { return "CameraComponent"; }
};

// === SceneManager (abstract base) ===
class SceneManager {
public:
    virtual ~SceneManager() = default;

    // Object lifecycle
    virtual uint32_t add_object(const SceneObjectGPU& obj) = 0;
    virtual void     update_object(uint32_t index, const SceneObjectGPU& obj) = 0;
    virtual void     remove_object(uint32_t index) = 0;
    virtual uint32_t object_count() const = 0;

    // Component lifecycle
    virtual uint32_t register_component(std::shared_ptr<Component> component) = 0;
    virtual void     unregister_component(uint32_t componentId) = 0;
    virtual std::shared_ptr<Component> get_component(uint32_t componentId) const = 0;

    // Per-frame camera / view
    virtual void set_view(const SceneView& view) = 0;

    // Full-frame render: upload → cull → compact → draw
    virtual void render(rhi::GraphicsCommandList* gfxCmd,
                        rhi::ComputeCommandList*  computeCmd,
                        rhi::CopyCommandList*     copyCmd) = 0;

    // Forward render of MeshComponents (simple direct draw path)
    virtual void render_forward(rhi::GraphicsCommandList* gfxCmd,
                                rhi::GraphicsPipeline* pipeline) = 0;
};

// === OctreeSceneManager (concrete, octree-based spatial partitioning) ===
struct OctreeNode; // forward declaration for pointer members

class OctreeSceneManager : public SceneManager {
public:
    OctreeSceneManager(std::shared_ptr<rhi::Device> device,
                       const math::BoundingBox& sceneBounds,
                       uint32_t maxObjectsPerNode = 8,
                       uint32_t maxDepth = 8);
    ~OctreeSceneManager() override;

    uint32_t add_object(const SceneObjectGPU& obj) override;
    void     update_object(uint32_t index, const SceneObjectGPU& obj) override;
    void     remove_object(uint32_t index) override;
    uint32_t object_count() const override { return m_objectCount; }

    void set_view(const SceneView& view) override;
    void render(rhi::GraphicsCommandList* gfxCmd,
                rhi::ComputeCommandList*  computeCmd,
                rhi::CopyCommandList*     copyCmd) override;

    // Component lifecycle
    uint32_t register_component(std::shared_ptr<Component> component) override;
    void     unregister_component(uint32_t componentId) override;
    std::shared_ptr<Component> get_component(uint32_t componentId) const override;

    // Forward render
    void render_forward(rhi::GraphicsCommandList* gfxCmd,
                        rhi::GraphicsPipeline* pipeline) override;

    // Direct GPU buffer access (for external binding / debug)
    rhi::Buffer* scene_buffer() const { return m_sceneBuffer.get(); }
    rhi::Buffer* visible_buffer() const { return m_visibleBuffer.get(); }
    rhi::Buffer* indirect_buffer() const { return m_indirectBuffer.get(); }

private:
    OctreeNode* m_root;

    std::shared_ptr<rhi::Device> m_device;
    uint32_t m_maxObjectsPerNode;
    uint32_t m_maxDepth;

    // Object storage (CPU)
    std::vector<SceneObjectGPU> m_objects;
    std::vector<uint32_t>       m_freeSlots;
    std::vector<OctreeNode*>    m_objectToNode;
    uint32_t                    m_objectCount = 0;

    // GPU buffers
    rhi::BufferPtr m_sceneBuffer;
    rhi::BufferPtr m_visibleBuffer;
    rhi::BufferPtr m_indirectBuffer;

    // GPU pipeline components
    std::unique_ptr<CullingJob>        m_cullingJob;
    std::unique_ptr<IndirectDrawManager> m_indirectDraw;

    // Per-frame state
    SceneView         m_view;
    math::Frustum     m_frustum;
    bool              m_viewDirty = true;

    // Reusable buffers for CPU culling results
    std::vector<uint32_t>        m_visibleIndices;
    std::vector<SceneObjectGPU>  m_visibleObjects;

    // Component storage
    std::vector<std::shared_ptr<Component>> m_components;
    std::vector<uint32_t>                   m_componentFreeSlots;
    std::vector<uint32_t>                   m_componentObjectMap; // componentId -> sceneObjectId, UINT32_MAX if n/a

    // Internal helpers
    OctreeNode* find_insert_node(OctreeNode* node, const math::BoundingSphere& sphere, uint32_t depth);
    void        insert_object_into_tree(uint32_t index);
    void        remove_object_from_tree(uint32_t index);
    void        collect_visible(OctreeNode* node, std::vector<uint32_t>& outIndices) const;
    void        destroy_tree(OctreeNode* node);
    math::BoundingSphere get_bounding_sphere(uint32_t index) const;
};

// === High-level RenderScene ===
class RenderScene {
public:
    explicit RenderScene(std::shared_ptr<rhi::Device> device);
    explicit RenderScene(std::shared_ptr<rhi::Device> device,
                         std::unique_ptr<SceneManager> sceneManager);

    // Add procedurally generated objects
    uint32_t add_object(const SceneObjectGPU& obj);

    // Component management
    uint32_t register_component(std::shared_ptr<Component> component);
    void     unregister_component(uint32_t componentId);
    std::shared_ptr<Component> get_component(uint32_t componentId) const;

    // Per-frame update
    void update(const SceneView& view);

    // Render a frame
    void render(rhi::GraphicsCommandList* gfxCmd,
                rhi::ComputeCommandList*  computeCmd,
                rhi::CopyCommandList*     copyCmd);

    // Forward render (simple direct draw of MeshComponents)
    void render_forward(rhi::GraphicsCommandList* gfxCmd,
                        rhi::GraphicsPipeline* pipeline);

private:
    std::shared_ptr<rhi::Device> m_device;
    std::unique_ptr<SceneManager> m_sceneManager;
};

} // namespace aether::renderer

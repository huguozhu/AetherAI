module;
#include <vector>
#include <memory>
#include <cstdint>
#include <cstring>
#include <algorithm>

module aether.renderer;

import aether.core;
import aether.rhi;

namespace aether::renderer {

// ============================================================================
// OctreeNode — internal, defined entirely in this TU
// ============================================================================

struct OctreeNode {
    math::BoundingBox bounds;
    std::vector<uint32_t> objectIndices;
    std::unique_ptr<OctreeNode> children[8] = {};
    bool isLeaf = true;

    explicit OctreeNode(const math::BoundingBox& b) : bounds(b) {}

    // Determine which child octant (0-7) the sphere center falls into.
    // Returns -1 if the sphere straddles a partition boundary.
    int get_child_index(const math::BoundingSphere& sphere) const {
        math::float3 mid = bounds.center();
        int cx = (sphere.center.x >= mid.x) ? 1 : 0;
        int cy = (sphere.center.y >= mid.y) ? 1 : 0;
        int cz = (sphere.center.z >= mid.z) ? 1 : 0;
        int ci = cx | (cy << 1) | (cz << 2);

        if (!children[ci]) return -1;

        const auto& c = children[ci]->bounds;
        if (sphere.center.x - sphere.radius < c.min.x ||
            sphere.center.x + sphere.radius > c.max.x ||
            sphere.center.y - sphere.radius < c.min.y ||
            sphere.center.y + sphere.radius > c.max.y ||
            sphere.center.z - sphere.radius < c.min.z ||
            sphere.center.z + sphere.radius > c.max.z) {
            return -1;
        }
        return ci;
    }

    void subdivide(uint32_t maxDepth, uint32_t currentDepth) {
        if (!isLeaf || currentDepth >= maxDepth) return;

        math::float3 mid = bounds.center();
        for (int i = 0; i < 8; ++i) {
            math::float3 childMin, childMax;
            childMin.x = (i & 1) ? mid.x : bounds.min.x;
            childMax.x = (i & 1) ? bounds.max.x : mid.x;
            childMin.y = (i & 2) ? mid.y : bounds.min.y;
            childMax.y = (i & 2) ? bounds.max.y : mid.y;
            childMin.z = (i & 4) ? mid.z : bounds.min.z;
            childMax.z = (i & 4) ? bounds.max.z : mid.z;
            children[i] = std::make_unique<OctreeNode>(
                math::BoundingBox{childMin, childMax});
        }
        isLeaf = false;
    }
};

// ============================================================================
// OctreeSceneManager
// ============================================================================

OctreeSceneManager::OctreeSceneManager(std::shared_ptr<rhi::Device> device,
                                       const math::BoundingBox& sceneBounds,
                                       uint32_t maxObjectsPerNode,
                                       uint32_t maxDepth)
    : m_device(std::move(device))
    , m_maxObjectsPerNode(maxObjectsPerNode)
    , m_maxDepth(maxDepth)
{
    m_root = new OctreeNode(sceneBounds);

    m_objects.reserve(kMaxObjects);
    m_objectToNode.reserve(kMaxObjects);
    m_visibleIndices.reserve(kMaxObjects);
    m_visibleObjects.reserve(kMaxObjects);

    auto sceneDesc = rhi::BufferDesc{
        .size = sizeof(SceneObjectGPU) * kMaxObjects,
        .heap = rhi::HeapType::Default,
        .bindFlags = rhi::BindFlags::ShaderResource,
    };
    m_sceneBuffer = m_device->create_buffer(sceneDesc);

    auto visibleDesc = rhi::BufferDesc{
        .size = sizeof(VisibleInfo) * kMaxObjects,
        .heap = rhi::HeapType::Default,
        .bindFlags = rhi::BindFlags::UnorderedAccess,
    };
    m_visibleBuffer = m_device->create_buffer(visibleDesc);

    auto indirectDesc = rhi::BufferDesc{
        .size = sizeof(DrawArg) * kMaxObjects,
        .heap = rhi::HeapType::Default,
        .bindFlags = rhi::BindFlags::UnorderedAccess,
    };
    m_indirectBuffer = m_device->create_buffer(indirectDesc);

    m_cullingJob = std::make_unique<CullingJob>(
        m_device, m_sceneBuffer, m_visibleBuffer, kMaxObjects);

    m_indirectDraw = std::make_unique<IndirectDrawManager>(
        m_device, m_sceneBuffer, m_visibleBuffer, m_indirectBuffer);

    aether::log::info("OctreeSceneManager: created (bounds: {:.1f},{:.1f},{:.1f} – {:.1f},{:.1f},{:.1f})",
                       sceneBounds.min.x, sceneBounds.min.y, sceneBounds.min.z,
                       sceneBounds.max.x, sceneBounds.max.y, sceneBounds.max.z);
}

OctreeSceneManager::~OctreeSceneManager() {
    destroy_tree(m_root);
    m_root = nullptr;
}

// ============================================================================
// Object lifecycle
// ============================================================================

uint32_t OctreeSceneManager::add_object(const SceneObjectGPU& obj) {
    uint32_t index;
    if (!m_freeSlots.empty()) {
        index = m_freeSlots.back();
        m_freeSlots.pop_back();
        m_objects[index] = obj;
    } else {
        index = static_cast<uint32_t>(m_objects.size());
        m_objects.push_back(obj);
    }
    m_objectCount++;
    insert_object_into_tree(index);
    m_viewDirty = true;
    return index;
}

void OctreeSceneManager::update_object(uint32_t index, const SceneObjectGPU& obj) {
    if (index >= m_objects.size()) return;
    remove_object_from_tree(index);
    m_objects[index] = obj;
    insert_object_into_tree(index);
    m_viewDirty = true;
}

void OctreeSceneManager::remove_object(uint32_t index) {
    if (index >= m_objects.size()) return;
    remove_object_from_tree(index);
    m_freeSlots.push_back(index);
    m_objectCount--;
    m_viewDirty = true;
}

// ============================================================================
// View / frame update
// ============================================================================

void OctreeSceneManager::set_view(const SceneView& view) {
    m_view = view;
    m_frustum = math::Frustum::from_view_proj(view.viewProj);
    m_viewDirty = true;
}

// ============================================================================
// Render — full pipeline: CPU octree cull → upload → GPU cull → compact → draw
// ============================================================================

void OctreeSceneManager::render(rhi::GraphicsCommandList* gfxCmd,
                                 rhi::ComputeCommandList*  computeCmd,
                                 rhi::CopyCommandList*     copyCmd)
{
    if (!gfxCmd || !computeCmd || !copyCmd) return;
    if (m_objectCount == 0) return;

    // 1. CPU octree frustum culling
    m_visibleIndices.clear();
    collect_visible(m_root, m_visibleIndices);

    uint32_t visibleCount = static_cast<uint32_t>(m_visibleIndices.size());

    // 2. Compact visible objects and upload to GPU scene buffer
    m_visibleObjects.resize(visibleCount);
    for (uint32_t i = 0; i < visibleCount; ++i) {
        m_visibleObjects[i] = m_objects[m_visibleIndices[i]];
    }
    if (visibleCount > 0) {
        copyCmd->upload_buffer(
            m_sceneBuffer.get(), 0,
            m_visibleObjects.data(),
            visibleCount * sizeof(SceneObjectGPU));
    }

    // 3. GPU culling (LOD selection on CPU-visible subset)
    m_cullingJob->update_view(m_view);
    m_cullingJob->bind(computeCmd);
    {
        uint32_t groupsX = (visibleCount + 63) / 64;
        computeCmd->dispatch(groupsX, 1, 1);
    }

    // 4. GPU compaction
    m_indirectDraw->dispatch_compact(computeCmd, visibleCount);

    // 5. Indirect draw
    m_indirectDraw->draw(gfxCmd, nullptr, visibleCount);
}

// ============================================================================
// Forward render — simple direct draw of visible MeshComponents
// ============================================================================

void OctreeSceneManager::render_forward(rhi::GraphicsCommandList* gfxCmd,
                                         rhi::GraphicsPipeline* pipeline) {
    if (!gfxCmd || !pipeline) return;

    gfxCmd->bind_pipeline(pipeline);

    // Iterate all registered components, draw visible MeshComponents
    for (size_t i = 0; i < m_components.size(); ++i) {
        if (!m_components[i]) continue;

        auto* mesh = dynamic_cast<MeshComponent*>(m_components[i].get());
        if (!mesh || !mesh->has_gpu_resources()) continue;

        // Frustum culling
        if (!m_frustum.contains(mesh->bounding_sphere()))
            continue;

        auto* vb = mesh->vertex_buffer();
        if (vb) {
            constexpr uint32_t kPositionStride = sizeof(float) * 3; // float3 position
            gfxCmd->ia_set_vertex_buffer(0, vb, kPositionStride);
            gfxCmd->draw(mesh->vertex_count(), 1, 0, 0);
        }
    }
}

// ============================================================================
// Component lifecycle
// ============================================================================

uint32_t OctreeSceneManager::register_component(std::shared_ptr<Component> component) {
    if (!component) return UINT32_MAX;

    uint32_t id;
    if (!m_componentFreeSlots.empty()) {
        id = m_componentFreeSlots.back();
        m_componentFreeSlots.pop_back();
        m_components[id] = std::move(component);
    } else {
        id = static_cast<uint32_t>(m_components.size());
        m_components.push_back(component);
        m_componentObjectMap.push_back(UINT32_MAX);
    }

    // If MeshComponent, create GPU resources and add to scene pipeline
    if (auto* mesh = dynamic_cast<MeshComponent*>(m_components[id].get())) {
        mesh->create_gpu_resources(m_device.get());
        SceneObjectGPU sceneObj = mesh->to_scene_object(id);
        uint32_t objId = add_object(sceneObj);
        if (id < m_componentObjectMap.size()) {
            m_componentObjectMap[id] = objId;
        }
    }

    return id;
}

void OctreeSceneManager::unregister_component(uint32_t componentId) {
    if (componentId >= m_components.size() || !m_components[componentId]) return;

    // If MeshComponent, remove from scene pipeline
    if (componentId < m_componentObjectMap.size() &&
        m_componentObjectMap[componentId] != UINT32_MAX) {
        remove_object(m_componentObjectMap[componentId]);
        m_componentObjectMap[componentId] = UINT32_MAX;
    }

    m_components[componentId].reset();
    m_componentFreeSlots.push_back(componentId);
}

std::shared_ptr<Component> OctreeSceneManager::get_component(uint32_t componentId) const {
    if (componentId >= m_components.size()) return nullptr;
    return m_components[componentId];
}

// ============================================================================
// Internal helpers — octree
// ============================================================================

math::BoundingSphere OctreeSceneManager::get_bounding_sphere(uint32_t index) const {
    if (index >= m_objects.size()) return {{0, 0, 0}, 0};
    const auto& s = m_objects[index].boundingSphere;
    return {{s.x, s.y, s.z}, s.w};
}

OctreeNode* OctreeSceneManager::find_insert_node(OctreeNode* node,
                                                  const math::BoundingSphere& sphere,
                                                  uint32_t /*depth*/)
{
    if (!node || node->isLeaf) return node;
    int ci = node->get_child_index(sphere);
    if (ci < 0) return node;
    return find_insert_node(node->children[ci].get(), sphere, 0);
}

void OctreeSceneManager::insert_object_into_tree(uint32_t index) {
    if (index >= m_objects.size()) return;

    math::BoundingSphere sphere = get_bounding_sphere(index);
    OctreeNode* node = find_insert_node(m_root, sphere, 0);
    if (!node) return;

    node->objectIndices.push_back(index);
    if (index >= m_objectToNode.size()) {
        m_objectToNode.resize(index + 1, nullptr);
    }
    m_objectToNode[index] = node;

    if (node->isLeaf && node->objectIndices.size() > m_maxObjectsPerNode) {
        node->subdivide(m_maxDepth, 0);
        if (!node->isLeaf) {
            std::vector<uint32_t> remaining;
            for (uint32_t idx : node->objectIndices) {
                math::BoundingSphere s = get_bounding_sphere(idx);
                int ci = node->get_child_index(s);
                if (ci >= 0) {
                    node->children[ci]->objectIndices.push_back(idx);
                    if (idx < m_objectToNode.size()) {
                        m_objectToNode[idx] = node->children[ci].get();
                    }
                } else {
                    remaining.push_back(idx);
                }
            }
            node->objectIndices = std::move(remaining);
        }
    }
}

void OctreeSceneManager::remove_object_from_tree(uint32_t index) {
    if (index >= m_objectToNode.size()) return;
    OctreeNode* node = m_objectToNode[index];
    if (!node) return;
    auto& indices = node->objectIndices;
    for (size_t i = 0; i < indices.size(); ++i) {
        if (indices[i] == index) {
            indices[i] = indices.back();
            indices.pop_back();
            break;
        }
    }
    m_objectToNode[index] = nullptr;
}

void OctreeSceneManager::collect_visible(OctreeNode* node,
                                          std::vector<uint32_t>& outIndices) const
{
    if (!node) return;

    math::Containment c = m_frustum.contains(node->bounds);
    if (c == math::Containment::Outside) return;

    for (uint32_t idx : node->objectIndices) {
        if (c == math::Containment::Inside || m_frustum.contains(get_bounding_sphere(idx))) {
            outIndices.push_back(idx);
        }
    }

    if (!node->isLeaf) {
        for (int i = 0; i < 8; ++i) {
            if (node->children[i]) {
                collect_visible(node->children[i].get(), outIndices);
            }
        }
    }
}

void OctreeSceneManager::destroy_tree(OctreeNode* node) {
    if (!node) return;
    if (!node->isLeaf) {
        for (int i = 0; i < 8; ++i) {
            if (node->children[i]) {
                for (uint32_t idx : node->children[i]->objectIndices) {
                    if (idx < m_objectToNode.size()) m_objectToNode[idx] = nullptr;
                }
                destroy_tree(node->children[i].get());
                node->children[i].reset();
            }
        }
    }
    for (uint32_t idx : node->objectIndices) {
        if (idx < m_objectToNode.size()) m_objectToNode[idx] = nullptr;
    }
    delete node;
}

} // namespace aether::renderer

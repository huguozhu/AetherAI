module;
#include <vector>
#include <cstring>
#include <cstdint>
#include <cstddef>

module aether.renderer;

import aether.core;
import aether.rhi;

namespace aether::renderer {

GPUSceneManager::GPUSceneManager(std::shared_ptr<rhi::Device> device)
    : m_device(std::move(device))
{
    // Create persistent GPU buffers

    // Scene object data buffer
    auto sceneDesc = rhi::BufferDesc{
        .size = sizeof(SceneObjectGPU) * kMaxObjects,
        .heap = rhi::HeapType::Default,
        .bindFlags = rhi::BindFlags::ShaderResource,
    };
    m_sceneBuffer = m_device->create_buffer(sceneDesc);

    // Visible buffer (culling output)
    auto visibleDesc = rhi::BufferDesc{
        .size = sizeof(VisibleInfo) * kMaxObjects,
        .heap = rhi::HeapType::Default,
        .bindFlags = rhi::BindFlags::UnorderedAccess,
    };
    m_visibleBuffer = m_device->create_buffer(visibleDesc);

    // Indirect draw args buffer
    auto indirectDesc = rhi::BufferDesc{
        .size = sizeof(DrawArg) * kMaxObjects,
        .heap = rhi::HeapType::Default,
        .bindFlags = rhi::BindFlags::UnorderedAccess,
    };
    m_indirectBuffer = m_device->create_buffer(indirectDesc);

    m_objects.resize(kMaxObjects);

    log::info("GPUSceneManager: created GPU buffers ({} objects)", kMaxObjects);
}

uint32_t GPUSceneManager::add_object(const SceneObjectGPU& obj) {
    if (m_objectCount >= kMaxObjects) {
        log::warn("GPUSceneManager: max objects reached ({})", kMaxObjects);
        return UINT32_MAX;
    }
    uint32_t index = m_objectCount++;
    m_objects[index] = obj;
    m_dirty = true;
    return index;
}

void GPUSceneManager::update_object(uint32_t index, const SceneObjectGPU& obj) {
    if (index < m_objectCount) {
        m_objects[index] = obj;
        m_dirty = true;
    }
}

void GPUSceneManager::remove_object(uint32_t index) {
    if (index < m_objectCount) {
        // Swap with last
        m_objects[index] = m_objects[m_objectCount - 1];
        m_objectCount--;
        m_dirty = true;
    }
}

void GPUSceneManager::upload_scene_data(rhi::CopyCommandList* copyCmd) {
    if (!m_dirty || !copyCmd) return;

    // Upload scene object data
    copyCmd->upload_buffer(
        m_sceneBuffer.get(), 0,
        m_objects.data(),
        m_objectCount * sizeof(SceneObjectGPU));

    m_dirty = false;

    log::debug("GPUSceneManager: uploaded {} objects", m_objectCount);
}

} // namespace aether::renderer

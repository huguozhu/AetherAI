module;
#include <vector>
#include <cstring>
#include <memory>
#include <cstdint>
#include <span>

module aether.renderer;

import aether.core;
import aether.rhi;

namespace aether::renderer {

void MeshComponent::set_positions(std::span<const float> positions) {
    m_positions.assign(positions.begin(), positions.end());
    m_vertexCount = static_cast<uint32_t>(positions.size() / 3);
}

void MeshComponent::set_normals(std::span<const float> normals) {
    m_normals.assign(normals.begin(), normals.end());
}

void MeshComponent::set_uvs(std::span<const float> uvs) {
    m_uvs.assign(uvs.begin(), uvs.end());
}

void MeshComponent::set_indices(std::span<const uint32_t> indices) {
    m_indices.assign(indices.begin(), indices.end());
    m_indexCount = static_cast<uint32_t>(indices.size());
}

void MeshComponent::set_world_matrix(const math::float4x4& matrix) {
    m_worldMatrix = matrix;
}

void MeshComponent::set_bounding_sphere(const math::BoundingSphere& sphere) {
    m_boundingSphere = sphere;
}

void MeshComponent::add_lod(std::span<const float> positions,
                            std::span<const uint32_t> indices,
                            float distanceThreshold)
{
    LODLevel lod;
    lod.positions.assign(positions.begin(), positions.end());
    lod.indices.assign(indices.begin(), indices.end());
    lod.distanceThreshold = distanceThreshold;
    m_lods.push_back(std::move(lod));
}

void MeshComponent::create_gpu_resources(rhi::Device* device) {
    if (!device || m_vertexCount == 0) return;

    auto vbDesc = rhi::BufferDesc{
        .size = m_positions.size() * sizeof(float),
        .heap = rhi::HeapType::Upload,
        .bindFlags = rhi::BindFlags::VertexBuffer,
    };
    m_vertexBuffer = device->create_buffer(vbDesc);
    if (!m_vertexBuffer) {
        aether::log::error("MeshComponent: failed to create vertex buffer");
        return;
    }

    // Fill vertex buffer immediately (Upload heap)
    {
        void* mapped = m_vertexBuffer->map();
        if (mapped) {
            std::memcpy(mapped, m_positions.data(), m_positions.size() * sizeof(float));
            m_vertexBuffer->unmap();
        }
    }

    if (m_indexCount > 0) {
        auto ibDesc = rhi::BufferDesc{
            .size = m_indices.size() * sizeof(uint32_t),
            .heap = rhi::HeapType::Upload,
            .bindFlags = rhi::BindFlags::IndexBuffer,
        };
        m_indexBuffer = device->create_buffer(ibDesc);
        if (!m_indexBuffer) {
            aether::log::error("MeshComponent: failed to create index buffer");
            return;
        }

        void* mapped = m_indexBuffer->map();
        if (mapped) {
            std::memcpy(mapped, m_indices.data(), m_indices.size() * sizeof(uint32_t));
            m_indexBuffer->unmap();
        }
    }
}

void MeshComponent::upload(rhi::CopyCommandList* copyCmd) {
    if (!copyCmd || !m_vertexBuffer) return;

    copyCmd->upload_buffer(m_vertexBuffer.get(), 0,
                           m_positions.data(),
                           m_positions.size() * sizeof(float));

    if (m_indexBuffer && !m_indices.empty()) {
        copyCmd->upload_buffer(m_indexBuffer.get(), 0,
                               m_indices.data(),
                               m_indices.size() * sizeof(uint32_t));
    }
}

SceneObjectGPU MeshComponent::to_scene_object(uint32_t meshIndex) const {
    SceneObjectGPU obj{};
    obj.worldMatrix = m_worldMatrix;
    obj.boundingSphere.x = m_boundingSphere.center.x;
    obj.boundingSphere.y = m_boundingSphere.center.y;
    obj.boundingSphere.z = m_boundingSphere.center.z;
    obj.boundingSphere.w = m_boundingSphere.radius;
    obj.meshIndex = meshIndex;
    obj.lodCount = static_cast<uint32_t>((std::min)(m_lods.size(), static_cast<size_t>(kMaxLODs)));
    for (size_t i = 0; i < m_lods.size() && i < kMaxLODs; ++i) {
        obj.lodDistances[i] = m_lods[i].distanceThreshold;
    }
    return obj;
}

} // namespace aether::renderer

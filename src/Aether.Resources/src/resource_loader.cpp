module;
#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <string>
#include <string_view>

module aether.resources;

import <coroutine>;
import <memory>;
import <utility>;

import aether.core;
import aether.rhi;

namespace aether::resources {

ResourceLoader::ResourceLoader(std::shared_ptr<rhi::Device> device)
    : m_device(std::move(device)) {}

concurrency::Task<MeshResource> ResourceLoader::load_mesh_async(std::string_view path) {
    auto data = co_await concurrency::read_file_async(path);
    if (data.empty()) {
        log::error("ResourceLoader: failed to read mesh file: {}", path);
        co_return MeshResource{};
    }

    // Simple Wavefront OBJ parser for demonstration.
    struct Vertex { float pos[3]; float normal[3]; float uv[2]; };
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<float> positions;
    std::vector<float> texcoords;
    std::vector<float> normals;

    std::string text(reinterpret_cast<char*>(data.data()), data.size());
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == 'v' && line[1] == ' ') {
            float x, y, z;
            sscanf_s(line.c_str(), "v %f %f %f", &x, &y, &z);
            positions.push_back(x); positions.push_back(y); positions.push_back(z);
        } else if (line[0] == 'v' && line[1] == 't') {
            float u, v;
            sscanf_s(line.c_str(), "vt %f %f", &u, &v);
            texcoords.push_back(u); texcoords.push_back(v);
        } else if (line[0] == 'v' && line[1] == 'n') {
            float x, y, z;
            sscanf_s(line.c_str(), "vn %f %f %f", &x, &y, &z);
            normals.push_back(x); normals.push_back(y); normals.push_back(z);
        } else if (line[0] == 'f') {
            unsigned int v[4] = {}, vt[4] = {}, vn[4] = {};
            int count = sscanf_s(line.c_str(), "f %u/%u/%u %u/%u/%u %u/%u/%u %u/%u/%u",
                &v[0], &vt[0], &vn[0], &v[1], &vt[1], &vn[1],
                &v[2], &vt[2], &vn[2], &v[3], &vt[3], &vn[3]);
            int faceVerts = (count == 12) ? 4 : 3;
            for (int i = 0; i < faceVerts; ++i) {
                Vertex vert{};
                if (v[i] > 0 && (v[i] - 1) * 3 + 2 < positions.size()) {
                    vert.pos[0] = positions[(v[i] - 1) * 3];
                    vert.pos[1] = positions[(v[i] - 1) * 3 + 1];
                    vert.pos[2] = positions[(v[i] - 1) * 3 + 2];
                }
                if (vt[i] > 0 && (vt[i] - 1) * 2 + 1 < texcoords.size()) {
                    vert.uv[0] = texcoords[(vt[i] - 1) * 2];
                    vert.uv[1] = texcoords[(vt[i] - 1) * 2 + 1];
                }
                if (vn[i] > 0 && (vn[i] - 1) * 3 + 2 < normals.size()) {
                    vert.normal[0] = normals[(vn[i] - 1) * 3];
                    vert.normal[1] = normals[(vn[i] - 1) * 3 + 1];
                    vert.normal[2] = normals[(vn[i] - 1) * 3 + 2];
                }
                vertices.push_back(vert);
                indices.push_back(static_cast<uint32_t>(indices.size()));
            }
        }
    }

    if (vertices.empty()) {
        log::error("ResourceLoader: no vertices found in OBJ: {}", path);
        co_return MeshResource{};
    }

    // Create GPU buffers
    auto vertexStride = sizeof(Vertex);
    auto vbDesc = rhi::BufferDesc{
        .size = vertices.size() * vertexStride,
        .heap = rhi::HeapType::Default,
        .bindFlags = rhi::BindFlags::VertexBuffer,
    };
    auto vb = m_device->create_buffer(vbDesc);

    auto ibDesc = rhi::BufferDesc{
        .size = indices.size() * sizeof(uint32_t),
        .heap = rhi::HeapType::Default,
        .bindFlags = rhi::BindFlags::IndexBuffer,
    };
    auto ib = m_device->create_buffer(ibDesc);

    // Upload via copy queue
    {
        auto copyCmd = m_device->get_copy_queue();
        copyCmd->reset();
        copyCmd->upload_buffer(vb.get(), 0, vertices.data(), vertices.size() * vertexStride);
        copyCmd->upload_buffer(ib.get(), 0, indices.data(), indices.size() * sizeof(uint32_t));
        std::unique_ptr<rhi::CommandList> lists[] = {std::move(copyCmd)};
        m_device->execute_command_lists(lists);
        m_device->wait_for_idle();
    }

    MeshLOD lod;
    lod.vertexBuffer = std::move(vb);
    lod.indexBuffer = std::move(ib);
    lod.vertexCount = static_cast<uint32_t>(vertices.size());
    lod.indexCount = static_cast<uint32_t>(indices.size());
    lod.vertexStride = vertexStride;

    MeshResource mesh;
    mesh.lods.push_back(std::move(lod));
    mesh.name = path;

    log::info("ResourceLoader: loaded mesh '{}' ({} verts, {} indices)",
              path, mesh.lods[0].vertexCount, mesh.lods[0].indexCount);
    co_return mesh;
}

concurrency::Task<TextureResource> ResourceLoader::load_texture_async(std::string_view path) {
    auto fileData = co_await concurrency::read_file_async(path);
    if (fileData.empty()) {
        log::error("ResourceLoader: failed to read texture file: {}", path);
        co_return TextureResource{};
    }
    // For demonstration: create fallback 1x1 white texture
    auto tex = create_texture_from_data(1, 1, rhi::Format::R8G8B8A8_UNORM, nullptr, 4);
    co_return tex;
}

concurrency::Task<MaterialResource> ResourceLoader::load_material_async(std::string_view path) {
    MaterialResource mat;
    mat.name = path;
    co_return mat;
}

std::vector<std::byte> ResourceLoader::load_file_sync(std::string_view path) {
    std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
    if (!file) {
        log::error("ResourceLoader: cannot open file: {}", path);
        return {};
    }
    auto size = file.tellg();
    file.seekg(0);
    std::vector<std::byte> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

TextureResource ResourceLoader::create_texture_from_data(
    uint32_t width, uint32_t height, rhi::Format format,
    const void* data, size_t dataSize)
{
    auto texDesc = rhi::TextureDesc{
        .width = width,
        .height = height,
        .depth = 1,
        .mipLevels = 1,
        .format = format,
    };
    auto texture = m_device->create_texture(texDesc);
    if (!texture) {
        log::error("ResourceLoader: failed to create texture {}x{}", width, height);
        return {};
    }

    if (data && dataSize > 0) {
        auto copyCmd = m_device->get_copy_queue();
        copyCmd->reset();
        copyCmd->upload_texture(texture.get(), data, dataSize);
        std::unique_ptr<rhi::CommandList> lists[] = {std::move(copyCmd)};
        m_device->execute_command_lists(lists);
        m_device->wait_for_idle();
    }

    return TextureResource{
        .texture = std::move(texture),
        .width = width,
        .height = height,
        .format = format,
    };
}

} // namespace aether::resources

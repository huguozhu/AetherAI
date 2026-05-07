module;
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <cstdint>
#include <cstddef>

export module aether.resources;

import <coroutine>;

import aether.core;
import aether.rhi;

export namespace aether::resources {

// === Mesh ===
struct MeshLOD {
    rhi::BufferPtr vertexBuffer;
    rhi::BufferPtr indexBuffer;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;
};

struct MeshResource {
    std::vector<MeshLOD> lods;
    std::string name;
    bool is_valid() const { return !lods.empty() && lods[0].vertexCount > 0; }
};

// === Texture ===
struct TextureResource {
    rhi::TexturePtr texture;
    uint32_t width = 0;
    uint32_t height = 0;
    rhi::Format format = rhi::Format::R8G8B8A8_UNORM;
    bool is_valid() const { return texture != nullptr; }
};

// === Material ===
struct MaterialResource {
    std::string name;
    TextureResource baseColor;
    TextureResource normalMap;
    TextureResource metallicRoughness;
    TextureResource emissiveTexture;
    math::float4 baseColorFactor{1, 1, 1, 1};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    math::float3 emissiveFactor{0, 0, 0};
    bool doubleSided = false;
    bool alphaMask = false;
    float alphaCutoff = 0.5f;
};

// === ResourceLoader ===
class ResourceLoader {
public:
    ResourceLoader(std::shared_ptr<rhi::Device> device);

    concurrency::Task<MeshResource>     load_mesh_async(std::string_view path);
    concurrency::Task<TextureResource>  load_texture_async(std::string_view path);
    concurrency::Task<MaterialResource> load_material_async(std::string_view path);

    // Synchronous helpers for simple data
    std::vector<std::byte> load_file_sync(std::string_view path);
    TextureResource        create_texture_from_data(uint32_t width, uint32_t height, rhi::Format format, const void* data, size_t dataSize);

private:
    std::shared_ptr<rhi::Device> m_device;
};

} // namespace aether::resources

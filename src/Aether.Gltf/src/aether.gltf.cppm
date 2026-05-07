module;
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <cstddef>

export module aether.gltf;

import aether.core;
import aether.rhi;
import aether.resources;
import aether.renderer;

export namespace aether::gltf {

struct GltfMaterial {
    std::string name;
    resources::TextureResource baseColorTexture;
    resources::TextureResource normalTexture;
    resources::TextureResource metallicRoughnessTexture;
    resources::TextureResource emissiveTexture;
    math::float4 baseColorFactor{1, 1, 1, 1};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    math::float3 emissiveFactor{0, 0, 0};
    bool doubleSided = false;
    bool alphaMask = false;
    float alphaCutoff = 0.5f;
};

struct GltfScene {
    std::vector<std::shared_ptr<renderer::MeshComponent>> meshes;
    std::vector<std::shared_ptr<renderer::CameraComponent>> cameras;
    std::vector<std::shared_ptr<renderer::LightComponent>> lights;
    std::vector<GltfMaterial> materials;
    bool is_valid() const { return !meshes.empty() || !cameras.empty() || !lights.empty(); }
};

class GltfImporter {
public:
    explicit GltfImporter(std::shared_ptr<rhi::Device> device);

    GltfScene load_from_file(const std::string& path);
    GltfScene load_from_memory(const std::vector<std::byte>& data);

private:
    std::shared_ptr<rhi::Device> m_device;
};

} // namespace aether::gltf

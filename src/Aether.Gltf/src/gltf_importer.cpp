module;
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <vector>
#include <memory>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <filesystem>
#include <optional>
#include <algorithm>
#include <stb_image.h>

module aether.gltf;

import aether.core;
import aether.rhi;
import aether.resources;
import aether.renderer;

namespace aether::gltf {
namespace {

// ── helpers ────────────────────────────────────────────────────────────────

math::float4x4 trs_to_matrix(const std::array<float, 3>& t,
                              const std::array<float, 4>& r,
                              const std::array<float, 3>& s) {
    float x = r[0], y = r[1], z = r[2], w = r[3];
    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, xz = x * z, yz = y * z;
    float wx = w * x, wy = w * y, wz = w * z;

    math::float4x4 m = math::float4x4::identity();
    m.m[0][0] = (1 - 2 * (yy + zz)) * s[0];
    m.m[0][1] = (2 * (xy + wz)) * s[0];
    m.m[0][2] = (2 * (xz - wy)) * s[0];
    m.m[1][0] = (2 * (xy - wz)) * s[1];
    m.m[1][1] = (1 - 2 * (xx + zz)) * s[1];
    m.m[1][2] = (2 * (yz + wx)) * s[1];
    m.m[2][0] = (2 * (xz + wy)) * s[2];
    m.m[2][1] = (2 * (yz - wx)) * s[2];
    m.m[2][2] = (1 - 2 * (xx + yy)) * s[2];
    m.m[3][0] = t[0];
    m.m[3][1] = t[1];
    m.m[3][2] = t[2];
    return m;
}

math::float4x4 mat4x4_from_array(const std::array<float, 16>& src) {
    math::float4x4 m{};
    std::memcpy(m.m, src.data(), 16 * sizeof(float));
    return m;
}

math::float4x4 node_world_transform(const fastgltf::Asset& asset,
                                     const fastgltf::Node& node,
                                     const math::float4x4& parentWorld) {
    (void)asset;
    math::float4x4 local = math::float4x4::identity();
    if (auto* trs = std::get_if<fastgltf::TRS>(&node.transform)) {
        local = trs_to_matrix(trs->translation, trs->rotation, trs->scale);
    } else if (auto* mat = std::get_if<std::array<fastgltf::num, 16>>(&node.transform)) {
        local = mat4x4_from_array(*mat);
    }

    // Multiply parentWorld * local (column-major: parent then local)
    math::float4x4 world{};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float sum = 0;
            for (int k = 0; k < 4; ++k)
                sum += parentWorld.m[row][k] * local.m[k][col];
            world.m[row][col] = sum;
        }
    }
    return world;
}

const std::byte* get_buffer_data(const fastgltf::Buffer& buffer) {
    if (auto* arr = std::get_if<fastgltf::sources::Array>(&buffer.data))
        return reinterpret_cast<const std::byte*>(arr->bytes.data());
    if (auto* vec = std::get_if<fastgltf::sources::Vector>(&buffer.data))
        return reinterpret_cast<const std::byte*>(vec->bytes.data());
    return nullptr;
}

std::vector<float> read_accessor_floats(const fastgltf::Asset& asset,
                                         const fastgltf::Accessor& accessor,
                                         size_t componentsPerElement) {
    std::vector<float> result(accessor.count * componentsPerElement, 0.0f);
    fastgltf::copyFromAccessor<float>(asset, accessor, result.data());
    return result;
}

std::vector<uint32_t> read_accessor_uints(const fastgltf::Asset& asset,
                                           const fastgltf::Accessor& accessor) {
    std::vector<uint32_t> result(accessor.count);
    size_t i = 0;
    if (accessor.componentType == fastgltf::ComponentType::UnsignedShort) {
        fastgltf::iterateAccessor<uint16_t>(asset, accessor,
            [&](uint16_t val) { result[i++] = static_cast<uint32_t>(val); });
    } else if (accessor.componentType == fastgltf::ComponentType::UnsignedByte) {
        fastgltf::iterateAccessor<uint8_t>(asset, accessor,
            [&](uint8_t val) { result[i++] = static_cast<uint32_t>(val); });
    } else {
        fastgltf::copyFromAccessor<uint32_t>(asset, accessor, result.data());
    }
    return result;
}

resources::TextureResource load_image_texture(rhi::Device* device,
                                               const fastgltf::Asset& asset,
                                               const fastgltf::Image& image,
                                               const std::filesystem::path& baseDir) {
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = nullptr;

    auto loadFromMemory = [&](const unsigned char* data, size_t size) {
        pixels = stbi_load_from_memory(data, static_cast<int>(size),
                                        &width, &height, &channels, 4);
    };

    // Image data can be in several forms: BufferView, URI, Array, etc.
    if (auto* bvSrc = std::get_if<fastgltf::sources::BufferView>(&image.data)) {
        auto& bv = asset.bufferViews[bvSrc->bufferViewIndex];
        if (bv.bufferIndex < asset.buffers.size()) {
            auto* bufData = get_buffer_data(asset.buffers[bv.bufferIndex]);
            if (bufData) {
                loadFromMemory(
                    reinterpret_cast<const unsigned char*>(bufData) + bv.byteOffset,
                    bv.byteLength);
            }
        }
    } else if (auto* uriSrc = std::get_if<fastgltf::sources::URI>(&image.data)) {
        auto fullPath = baseDir / uriSrc->uri.fspath();
        pixels = stbi_load(fullPath.string().c_str(), &width, &height, &channels, 4);
    } else if (auto* arrSrc = std::get_if<fastgltf::sources::Array>(&image.data)) {
        loadFromMemory(arrSrc->bytes.data(), arrSrc->bytes.size());
    }

    if (!pixels) {
        log::warn("GltfImporter: failed to load image '{}'", image.name);
        return {};
    }

    // Upload to GPU
    auto texDesc = rhi::TextureDesc{
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .depth = 1,
        .mipLevels = 1,
        .format = rhi::Format::R8G8B8A8_UNORM,
    };
    auto texture = device->create_texture(texDesc);
    if (!texture) {
        log::error("GltfImporter: failed to create GPU texture {}x{}", width, height);
        stbi_image_free(pixels);
        return {};
    }

    size_t rowPitch = static_cast<size_t>(width) * 4;
    size_t totalSize = rowPitch * static_cast<size_t>(height);
    auto copyCmd = device->get_copy_queue();
    copyCmd->reset();
    copyCmd->upload_texture(texture.get(), pixels, totalSize);
    std::unique_ptr<rhi::CommandList> lists[] = {std::move(copyCmd)};
    device->execute_command_lists(lists);
    device->wait_for_idle();

    stbi_image_free(pixels);

    return resources::TextureResource{
        .texture = std::move(texture),
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .format = rhi::Format::R8G8B8A8_UNORM,
    };
}

GltfMaterial convert_material(rhi::Device* device,
                               const fastgltf::Asset& asset,
                               const fastgltf::Material& mat,
                               const std::filesystem::path& baseDir) {
    GltfMaterial result;
    result.name = mat.name;

    auto& pbr = mat.pbrData;
    result.baseColorFactor = {
        pbr.baseColorFactor[0],
        pbr.baseColorFactor[1],
        pbr.baseColorFactor[2],
        pbr.baseColorFactor[3],
    };
    result.metallicFactor = pbr.metallicFactor;
    result.roughnessFactor = pbr.roughnessFactor;

    if (pbr.baseColorTexture.has_value()) {
        auto& tex = asset.textures[pbr.baseColorTexture->textureIndex];
        if (tex.imageIndex.has_value()) {
            result.baseColorTexture = load_image_texture(
                device, asset, asset.images[*tex.imageIndex], baseDir);
        }
    }
    if (pbr.metallicRoughnessTexture.has_value()) {
        auto& tex = asset.textures[pbr.metallicRoughnessTexture->textureIndex];
        if (tex.imageIndex.has_value()) {
            result.metallicRoughnessTexture = load_image_texture(
                device, asset, asset.images[*tex.imageIndex], baseDir);
        }
    }

    if (mat.normalTexture.has_value()) {
        auto& tex = asset.textures[mat.normalTexture->textureIndex];
        if (tex.imageIndex.has_value()) {
            result.normalTexture = load_image_texture(
                device, asset, asset.images[*tex.imageIndex], baseDir);
        }
    }

    if (mat.emissiveTexture.has_value()) {
        auto& tex = asset.textures[mat.emissiveTexture->textureIndex];
        if (tex.imageIndex.has_value()) {
            result.emissiveTexture = load_image_texture(
                device, asset, asset.images[*tex.imageIndex], baseDir);
        }
    }

    result.emissiveFactor = {
        mat.emissiveFactor[0],
        mat.emissiveFactor[1],
        mat.emissiveFactor[2],
    };
    result.doubleSided = mat.doubleSided;
    result.alphaMask = mat.alphaMode == fastgltf::AlphaMode::Mask;
    result.alphaCutoff = static_cast<float>(mat.alphaCutoff);

    return result;
}

void traverse_node(const fastgltf::Asset& asset,
                    const fastgltf::Node& node,
                    const math::float4x4& parentWorld,
                    const std::vector<GltfMaterial>& materials,
                    const std::filesystem::path& baseDir,
                    GltfScene& outScene) {
    auto world = node_world_transform(asset, node, parentWorld);

    // ── Mesh ──
    if (node.meshIndex.has_value()) {
        auto& mesh = asset.meshes[*node.meshIndex];
        for (auto& primitive : mesh.primitives) {
            auto meshComponent = std::make_shared<renderer::MeshComponent>();

            // Helper: find attribute in SmallVector
            auto findAttr = [&](const char* name) -> std::optional<size_t> {
                for (auto& attr : primitive.attributes) {
                    if (attr.first == name) return attr.second;
                }
                return std::nullopt;
            };

            // Positions (transform from local to world space)
            {
                auto idx = findAttr("POSITION");
                if (idx.has_value()) {
                    auto& acc = asset.accessors[*idx];
                    auto data = read_accessor_floats(asset, acc, 3);
                    // Transform by world matrix so shader only needs viewProj
                    for (size_t vi = 0; vi < data.size(); vi += 3) {
                        float px = data[vi], py = data[vi+1], pz = data[vi+2];
                        data[vi]   = px * world.m[0][0] + py * world.m[1][0] + pz * world.m[2][0] + world.m[3][0];
                        data[vi+1] = px * world.m[0][1] + py * world.m[1][1] + pz * world.m[2][1] + world.m[3][1];
                        data[vi+2] = px * world.m[0][2] + py * world.m[1][2] + pz * world.m[2][2] + world.m[3][2];
                    }
                    meshComponent->set_positions(data);
                }
            }

            // Normals
            {
                auto idx = findAttr("NORMAL");
                if (idx.has_value()) {
                    auto& acc = asset.accessors[*idx];
                    auto data = read_accessor_floats(asset, acc, 3);
                    meshComponent->set_normals(data);
                }
            }

            // UVs (texcoord_0)
            {
                auto idx = findAttr("TEXCOORD_0");
                if (idx.has_value()) {
                    auto& acc = asset.accessors[*idx];
                    auto data = read_accessor_floats(asset, acc, 2);
                    meshComponent->set_uvs(data);
                }
            }

            // Indices
            if (primitive.indicesAccessor.has_value()) {
                auto& acc = asset.accessors[*primitive.indicesAccessor];
                auto data = read_accessor_uints(asset, acc);
                meshComponent->set_indices(data);
            }

            // World matrix is identity since positions baked to world space above
            meshComponent->set_world_matrix(math::float4x4::identity());

            // Bounding sphere from positions centroid
            {
                auto& pos = meshComponent->positions();
                if (!pos.empty()) {
                    math::float3 center{0, 0, 0};
                    float maxDistSq = 0;
                    size_t vertCount = pos.size() / 3;
                    for (size_t i = 0; i < vertCount; ++i) {
                        math::float3 p{pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]};
                        center.x += p.x;
                        center.y += p.y;
                        center.z += p.z;
                    }
                    center.x /= static_cast<float>(vertCount);
                    center.y /= static_cast<float>(vertCount);
                    center.z /= static_cast<float>(vertCount);
                    for (size_t i = 0; i < vertCount; ++i) {
                        math::float3 p{pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]};
                        float dx = p.x - center.x;
                        float dy = p.y - center.y;
                        float dz = p.z - center.z;
                        float dSq = dx * dx + dy * dy + dz * dz;
                        if (dSq > maxDistSq) maxDistSq = dSq;
                    }
                    meshComponent->set_bounding_sphere(
                        {center, std::sqrt(maxDistSq)});
                }
            }

            outScene.meshes.push_back(std::move(meshComponent));
        }
    }

    // ── Camera ──
    if (node.cameraIndex.has_value()) {
        auto& cam = asset.cameras[*node.cameraIndex];
        auto cameraComponent = std::make_shared<renderer::CameraComponent>();

        if (auto* proj = std::get_if<fastgltf::Camera::Perspective>(&cam.camera)) {
            cameraComponent->fov = proj->yfov * 180.0f / 3.14159265f;
            cameraComponent->aspect = proj->aspectRatio.value_or(1.0f);
            cameraComponent->nearPlane = static_cast<float>(proj->znear);
            cameraComponent->farPlane = static_cast<float>(proj->zfar.value_or(1000.0f));
        }

        // Position from world matrix
        cameraComponent->position = {world.m[3][0], world.m[3][1], world.m[3][2]};
        // Target: look along -Z of world transform
        cameraComponent->target = {
            world.m[3][0] - world.m[2][0],
            world.m[3][1] - world.m[2][1],
            world.m[3][2] - world.m[2][2],
        };

        outScene.cameras.push_back(std::move(cameraComponent));
    }

    // ── Light (KHR_lights_punctual) ──
    if (node.lightIndex.has_value()) {
        auto& gltfLight = asset.lights[*node.lightIndex];
        auto lightComponent = std::make_shared<renderer::LightComponent>();

        switch (gltfLight.type) {
            case fastgltf::LightType::Directional:
                lightComponent->type = renderer::LightType::Directional;
                break;
            case fastgltf::LightType::Point:
                lightComponent->type = renderer::LightType::Point;
                break;
            case fastgltf::LightType::Spot:
                lightComponent->type = renderer::LightType::Spot;
                break;
        }

        lightComponent->color = {
            gltfLight.color[0],
            gltfLight.color[1],
            gltfLight.color[2],
        };
        lightComponent->intensity = gltfLight.intensity;
        lightComponent->range = gltfLight.range.value_or(10.0f);

        // Position/direction from world transform
        lightComponent->position = {
            world.m[3][0], world.m[3][1], world.m[3][2]
        };
        lightComponent->direction = {
            world.m[2][0], world.m[2][1], world.m[2][2]
        };

        outScene.lights.push_back(std::move(lightComponent));
    }

    // ── Children ──
    for (auto childIdx : node.children) {
        if (childIdx < asset.nodes.size()) {
            traverse_node(asset, asset.nodes[childIdx], world,
                          materials, baseDir, outScene);
        }
    }
}

} // anonymous namespace

// ── public API ─────────────────────────────────────────────────────────────

GltfImporter::GltfImporter(std::shared_ptr<rhi::Device> device)
    : m_device(std::move(device)) {}

GltfScene GltfImporter::load_from_file(const std::string& path) {
    GltfScene scene;

    fastgltf::GltfDataBuffer gltfFile;
    if (!gltfFile.loadFromFile(path)) {
        log::error("GltfImporter: failed to open file '{}'", path);
        return scene;
    }

    auto baseDir = std::filesystem::path(path).parent_path();

    auto options = fastgltf::Options::LoadGLBBuffers |
                   fastgltf::Options::LoadExternalBuffers |
                   fastgltf::Options::LoadExternalImages;

    fastgltf::Parser parser(fastgltf::Extensions::KHR_lights_punctual);
    auto parsed = parser.loadGltf(&gltfFile, baseDir, options);
    if (parsed.error() != fastgltf::Error::None) {
        log::error("GltfImporter: failed to parse '{}'", path);
        return scene;
    }

    auto& asset = parsed.get();

    log::info("GltfImporter: loaded '{}' ({} meshes, {} cameras, {} materials)",
              path, asset.meshes.size(), asset.cameras.size(), asset.materials.size());

    // Convert materials
    for (auto& mat : asset.materials) {
        scene.materials.push_back(
            convert_material(m_device.get(), asset, mat, baseDir));
    }

    // Traverse scene nodes
    for (auto& sceneNode : asset.scenes) {
        for (auto rootIdx : sceneNode.nodeIndices) {
            if (rootIdx < asset.nodes.size()) {
                traverse_node(asset, asset.nodes[rootIdx],
                              math::float4x4::identity(),
                              scene.materials, baseDir, scene);
            }
        }
    }

    log::info("GltfImporter: converted {} meshes, {} cameras, {} lights",
              scene.meshes.size(), scene.cameras.size(), scene.lights.size());
    return scene;
}

GltfScene GltfImporter::load_from_memory(const std::vector<std::byte>& data) {
    GltfScene scene;

    // Create a data buffer from memory bytes
    fastgltf::GltfDataBuffer gltfFile(
        fastgltf::span<std::byte>(
            const_cast<std::byte*>(data.data()), data.size()));

    auto options = fastgltf::Options::LoadGLBBuffers;

    fastgltf::Parser parser(fastgltf::Extensions::KHR_lights_punctual);
    auto parsed = parser.loadGltf(&gltfFile, "", options);
    if (parsed.error() != fastgltf::Error::None) {
        log::error("GltfImporter: failed to parse memory buffer");
        return scene;
    }

    auto& asset = parsed.get();

    log::info("GltfImporter: loaded from memory ({} meshes, {} cameras)",
              asset.meshes.size(), asset.cameras.size());

    // Convert materials (no external images available from memory)
    for (auto& mat : asset.materials) {
        GltfMaterial gmat;
        gmat.name = mat.name;
        auto& pbr = mat.pbrData;
        gmat.baseColorFactor = {
            pbr.baseColorFactor[0],
            pbr.baseColorFactor[1],
            pbr.baseColorFactor[2],
            pbr.baseColorFactor[3],
        };
        gmat.metallicFactor = pbr.metallicFactor;
        gmat.roughnessFactor = pbr.roughnessFactor;
        scene.materials.push_back(std::move(gmat));
    }

    // Traverse nodes (no baseDir for memory loads)
    for (auto& sceneNode : asset.scenes) {
        for (auto rootIdx : sceneNode.nodeIndices) {
            if (rootIdx < asset.nodes.size()) {
                traverse_node(asset, asset.nodes[rootIdx],
                              math::float4x4::identity(),
                              scene.materials, "", scene);
            }
        }
    }

    return scene;
}

} // namespace aether::gltf

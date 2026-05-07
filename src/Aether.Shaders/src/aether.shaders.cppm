module;
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <span>
#include <cstdint>
#include <cstddef>
#include <unordered_map>

export module aether.shaders;

import aether.core;

export namespace aether::shaders {

// === Shader type ===
enum class ShaderType : uint8_t {
    Vertex,
    Pixel,
    Compute,
    Mesh,
    Amplification,
    RayGen,
    Miss,
    ClosestHit,
    AnyHit,
    Intersection,
    Callable,
};

// === Compilation target ===
enum class ShaderTarget : uint8_t {
    DXIL,   // D3D12
    SPIRV,  // Vulkan
    D3D,    // D3DCompile (fallback, generates DXBC)
};

// === Compilation descriptor ===
struct ShaderCompileDesc {
    std::string source;
    std::string entryPoint;
    ShaderType type = ShaderType::Vertex;
    ShaderTarget target = ShaderTarget::D3D;
    std::vector<std::string> defines;
    std::vector<std::string> includePaths;
};

// === Compilation result ===
class ShaderCompileResult {
public:
    ShaderCompileResult() = default;

    bool is_valid() const { return !m_bytecode.empty(); }
    std::span<const std::byte> get_bytecode() const { return m_bytecode; }
    const std::string& get_error() const { return m_error; }

    /// @returns true if any entry point uses ray tracing stages
    bool requires_ray_tracing() const { return m_requiresRayTracing; }
    /// @returns true if any entry point uses mesh or amplification shaders
    bool requires_mesh_shader() const { return m_requiresMeshShader; }
    /// @returns the number of entry points in the compiled shader
    uint32_t entry_point_count() const { return m_entryPointCount; }

    void set_bytecode(std::span<const std::byte> data) {
        m_bytecode.assign(data.begin(), data.end());
    }
    void set_error(std::string_view err) { m_error = std::string(err); }
    void set_requires_ray_tracing(bool v) { m_requiresRayTracing = v; }
    void set_requires_mesh_shader(bool v) { m_requiresMeshShader = v; }
    void set_entry_point_count(uint32_t count) { m_entryPointCount = count; }

private:
    std::vector<std::byte> m_bytecode;
    std::string m_error;
    bool m_requiresRayTracing = false;
    bool m_requiresMeshShader = false;
    uint32_t m_entryPointCount = 0;
};

// === Shader compiler ===
class ShaderCompiler {
public:
    ShaderCompiler();
    ~ShaderCompiler();

    bool is_slang_available() const { return m_slangAvailable; }
    ShaderCompileResult compile(const ShaderCompileDesc& desc);

private:
    ShaderCompileResult compile_with_d3d(const ShaderCompileDesc& desc);
    ShaderCompileResult compile_with_slang(const ShaderCompileDesc& desc);

    bool m_slangAvailable = false;
    void* m_slangGlobalSession = nullptr; // slang::IGlobalSession*
};

// === Shader library (cache) ===
class ShaderLibrary {
public:
    bool add(std::string_view name, ShaderCompileResult result);
    const ShaderCompileResult* get(std::string_view name) const;
    bool contains(std::string_view name) const;
    void clear();

private:
    std::unordered_map<std::string, ShaderCompileResult> m_cache;
};

// === Utility: profile to string helpers ===
const char* shader_type_to_profile(ShaderType type, ShaderTarget target);
const char* shader_type_to_entry(ShaderType type);

} // namespace aether::shaders

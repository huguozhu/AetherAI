module;
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <d3dcompiler.h>
#endif

module aether.shaders;

import aether.core;

namespace aether::shaders {

// === Profile/Entry helpers ===
const char* shader_type_to_profile(ShaderType type, ShaderTarget target) {
    if (target == ShaderTarget::SPIRV) return "spirv_1_6";
    const bool useSM66 = (target == ShaderTarget::DXIL);
    switch (type) {
        case ShaderType::Vertex:         return useSM66 ? "sm_6_6" : "vs_5_1";
        case ShaderType::Pixel:          return useSM66 ? "sm_6_6" : "ps_5_1";
        case ShaderType::Compute:        return useSM66 ? "sm_6_6" : "cs_5_1";
        case ShaderType::Mesh:           return "sm_6_6";
        case ShaderType::Amplification:  return "sm_6_6";
        default:                         return useSM66 ? "sm_6_6" : "cs_5_1";
    }
}

const char* shader_type_to_entry(ShaderType type) {
    return "main";
}

// === ShaderCompiler ===
ShaderCompiler::ShaderCompiler() {
#ifdef _WIN32
    auto slangLib = LoadLibraryA("slang.dll");
    if (slangLib) {
        m_slangAvailable = true;
        log::info("ShaderCompiler: Slang SDK detected");
    } else {
        log::info("ShaderCompiler: Slang not found, using D3DCompile fallback");
    }
#else
    log::info("ShaderCompiler: using D3DCompile fallback");
#endif
}

ShaderCompiler::~ShaderCompiler() = default;

ShaderCompileResult ShaderCompiler::compile(const ShaderCompileDesc& desc) {
    if (desc.source.empty()) {
        ShaderCompileResult result;
        result.set_error("ShaderCompiler: empty source");
        return result;
    }

    if (m_slangAvailable && desc.target != ShaderTarget::D3D) {
        auto result = compile_with_slang(desc);
        if (result.is_valid()) return result;
    }

    return compile_with_d3d(desc);
}

ShaderCompileResult ShaderCompiler::compile_with_d3d(const ShaderCompileDesc& desc) {
    ShaderCompileResult result;

#ifdef _WIN32
    std::string profile = shader_type_to_profile(desc.type, ShaderTarget::D3D);
    std::string entry = desc.entryPoint.empty() ? shader_type_to_entry(desc.type) : desc.entryPoint;

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    // Terminator-only macros
    D3D_SHADER_MACRO macros[] = {{nullptr, nullptr}};

    ID3DBlob* blob = nullptr;
    ID3DBlob* error = nullptr;

    HRESULT hr = D3DCompile(
        desc.source.data(),
        desc.source.size(),
        nullptr,
        macros,
        nullptr,
        entry.c_str(),
        profile.c_str(),
        flags, 0,
        &blob, &error);

    if (FAILED(hr)) {
        if (error) {
            result.set_error(static_cast<const char*>(error->GetBufferPointer()));
            log::error("ShaderCompiler D3DCompile failed: {}",
                       static_cast<const char*>(error->GetBufferPointer()));
            error->Release();
        } else {
            result.set_error("D3DCompile failed with unknown error");
            log::error("ShaderCompiler D3DCompile failed (HRESULT: 0x{:08X})",
                       static_cast<unsigned>(hr));
        }
        return result;
    }

    result.set_bytecode({static_cast<const std::byte*>(blob->GetBufferPointer()),
                         blob->GetBufferSize()});
    blob->Release();

    log::debug("ShaderCompiler: compiled '{}' ({}, {} bytes)",
               entry, profile, result.get_bytecode().size());
#else
    (void)desc;
    result.set_error("ShaderCompiler: D3DCompile not available on this platform");
#endif

    return result;
}

ShaderCompileResult ShaderCompiler::compile_with_slang(const ShaderCompileDesc& desc) {
    ShaderCompileResult result;
    (void)desc;
    result.set_error("ShaderCompiler: Slang backend not fully wired");
    return result;
}

} // namespace aether::shaders

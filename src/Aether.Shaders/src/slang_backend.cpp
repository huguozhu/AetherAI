#include "slang_backend.h"
#include <slang.h>
#include <string>
#include <cstring>
#include <cstddef>
#include <cstdlib>
#include <utility>

import aether.shaders;

namespace aether::shaders {

void* slang_create_global_session() {
    slang::IGlobalSession* session = nullptr;
    SlangGlobalSessionDesc desc = {};
    SlangResult sr = slang_createGlobalSession2(&desc, &session);
    if (SLANG_SUCCEEDED(sr) && session) {
        return session;
    }
    return nullptr;
}

void slang_destroy_global_session(void* session) {
    if (session) {
        static_cast<slang::IGlobalSession*>(session)->release();
    }
}

ShaderCompileResult slang_compile_shader(void* globalSessionVoid, const ShaderCompileDesc& desc) {
    ShaderCompileResult result;

    auto* globalSession = static_cast<slang::IGlobalSession*>(globalSessionVoid);
    if (!globalSession) {
        result.set_error("ShaderCompiler: Slang global session not initialized");
        return result;
    }

    // Select code generation target
    SlangCompileTarget targetFormat = SLANG_DXIL;
    const char* profileName = "sm_6_6";
    if (desc.target == ShaderTarget::SPIRV) {
        targetFormat = SLANG_SPIRV;
        profileName = "spirv_1_6";
    }

    slang::TargetDesc targetDesc = {};
    targetDesc.format = targetFormat;
    targetDesc.profile = globalSession->findProfile(profileName);

    // Create session with the target
    slang::SessionDesc sessionDesc = {};
    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;
    sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;

    // Add standard module search path from SLANG_PATH env var
    const char* slangPathEnv = getenv("SLANG_PATH");
    const char* searchPaths[4] = {};
    SlangInt searchPathCount = 0;

    if (slangPathEnv && strlen(slangPathEnv) > 0) {
        searchPaths[searchPathCount++] = slangPathEnv;
    }

    if (searchPathCount > 0) {
        sessionDesc.searchPaths = searchPaths;
        sessionDesc.searchPathCount = searchPathCount;
    }

    slang::ISession* session = nullptr;
    SlangResult sr = globalSession->createSession(sessionDesc, &session);
    if (SLANG_FAILED(sr) || !session) {
        result.set_error("ShaderCompiler: failed to create Slang session");
        return result;
    }

    // Determine entry point name
    std::string entryPointName = desc.entryPoint.empty()
        ? shader_type_to_entry(desc.type)
        : desc.entryPoint;

    // Load module from source string
    ISlangBlob* diagnostics = nullptr;
    slang::IModule* module = slang_loadModuleFromSource(
        session,
        "shaderModule",
        "shader.slang",
        desc.source.c_str(),
        desc.source.size(),
        &diagnostics);

    if (!module) {
        std::string errMsg = "ShaderCompiler: Slang failed to load module";
        if (diagnostics) {
            errMsg += ": ";
            errMsg += static_cast<const char*>(diagnostics->getBufferPointer());
            diagnostics->release();
        }
        result.set_error(errMsg);
        session->release();
        return result;
    }

    if (diagnostics) {
        diagnostics->release();
    }

    // Find entry point
    slang::IEntryPoint* entryPoint = nullptr;
    module->findEntryPointByName(entryPointName.c_str(), &entryPoint);

    if (!entryPoint) {
        SlangInt32 epCount = module->getDefinedEntryPointCount();
        if (epCount > 0) {
            module->getDefinedEntryPoint(0, &entryPoint);
        }

        if (!entryPoint) {
            result.set_error("ShaderCompiler: no entry point found in shader");
            module->release();
            session->release();
            return result;
        }
    }

    // Create composite component type (module + entry point)
    slang::IComponentType* components[] = { module, entryPoint };
    slang::IComponentType* composite = nullptr;
    sr = session->createCompositeComponentType(components, 2, &composite);

    slang::IComponentType* linked = nullptr;
    if (SLANG_SUCCEEDED(sr) && composite) {
        sr = composite->link(&linked);
    }

    if (SLANG_FAILED(sr) || !linked) {
        result.set_error("ShaderCompiler: Slang link failed");
        if (composite) composite->release();
        entryPoint->release();
        module->release();
        session->release();
        return result;
    }

    // Get compiled entry point code (DXIL or SPIR-V)
    ISlangBlob* code = nullptr;
    ISlangBlob* codeDiagnostics = nullptr;
    sr = linked->getEntryPointCode(0, 0, &code, &codeDiagnostics);

    if (SLANG_FAILED(sr) || !code) {
        std::string errMsg = "ShaderCompiler: failed to generate code";
        if (codeDiagnostics) {
            errMsg += ": ";
            errMsg += static_cast<const char*>(codeDiagnostics->getBufferPointer());
            codeDiagnostics->release();
        }
        result.set_error(errMsg);
        linked->release();
        if (composite) composite->release();
        entryPoint->release();
        module->release();
        session->release();
        return result;
    }

    if (codeDiagnostics) codeDiagnostics->release();

    // Copy bytecode to result
    result.set_bytecode({
        static_cast<const std::byte*>(code->getBufferPointer()),
        code->getBufferSize()
    });

    // Get reflection information
    slang::ProgramLayout* layout = linked->getLayout(0);
    if (layout) {
        SlangUInt epCount = layout->getEntryPointCount();
        result.set_entry_point_count(static_cast<uint32_t>(epCount));

        for (SlangUInt i = 0; i < epCount; ++i) {
            auto* epReflection = layout->getEntryPointByIndex(i);
            if (!epReflection) continue;

            SlangStage stage = epReflection->getStage();
            bool isRayTracing = (stage >= SLANG_STAGE_RAY_GENERATION &&
                                 stage <= SLANG_STAGE_CALLABLE);
            bool isMeshShader = (stage == SLANG_STAGE_MESH ||
                                 stage == SLANG_STAGE_AMPLIFICATION);

            if (isRayTracing) result.set_requires_ray_tracing(true);
            if (isMeshShader) result.set_requires_mesh_shader(true);
        }
    }

    // Cleanup
    code->release();
    linked->release();
    composite->release();
    entryPoint->release();
    module->release();
    session->release();

    return result;
}

} // namespace aether::shaders

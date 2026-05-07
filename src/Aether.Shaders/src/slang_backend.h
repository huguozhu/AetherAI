#pragma once

namespace aether::shaders {

struct ShaderCompileDesc;
class ShaderCompileResult;

/// Create a Slang global session, or nullptr if unavailable.
void* slang_create_global_session();

/// Destroy a Slang global session previously created by slang_create_global_session().
void slang_destroy_global_session(void* session);

/// Compile a shader using the Slang backend.
ShaderCompileResult slang_compile_shader(void* globalSession, const ShaderCompileDesc& desc);

} // namespace aether::shaders

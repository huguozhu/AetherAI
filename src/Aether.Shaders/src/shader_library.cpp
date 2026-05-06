module;
#include <string_view>
#include <string>
#include <unordered_map>

module aether.shaders;

import aether.core;

namespace aether::shaders {

bool ShaderLibrary::add(std::string_view name, ShaderCompileResult result) {
    if (!result.is_valid()) {
        log::warn("ShaderLibrary: rejecting invalid shader '{}'", name);
        return false;
    }
    auto [it, inserted] = m_cache.emplace(std::string(name), std::move(result));
    if (inserted) {
        log::debug("ShaderLibrary: cached '{}' ({} bytes)", name,
                   it->second.get_bytecode().size());
    }
    return inserted;
}

const ShaderCompileResult* ShaderLibrary::get(std::string_view name) const {
    auto it = m_cache.find(std::string(name));
    if (it != m_cache.end()) {
        return &it->second;
    }
    log::warn("ShaderLibrary: '{}' not found in cache", name);
    return nullptr;
}

bool ShaderLibrary::contains(std::string_view name) const {
    return m_cache.contains(std::string(name));
}

void ShaderLibrary::clear() {
    m_cache.clear();
    log::debug("ShaderLibrary: cache cleared");
}

} // namespace aether::shaders

#include "ShaderFactory.h"
#include "runtime/include/EnvironmentVariable.h"
#include "runtime/include/LogMacros.h"

#include <fstream>

namespace Engine::Renderer {

ShaderFactory::ShaderFactory(nvrhi::IDevice* device) : m_Device(device) {
  if (const auto configuredRoot = ReadEnvironmentVariable("GRAPHIC_ENGINE_ASSET_ROOT")) {
    m_AssetRoot = *configuredRoot;
  } else {
#ifdef ENGINE_DEFAULT_ASSET_ROOT
    m_AssetRoot = ENGINE_DEFAULT_ASSET_ROOT;
#else
    m_AssetRoot = std::filesystem::current_path();
#endif
  }
}

auto ShaderFactory::GetShader(const std::string& path, nvrhi::ShaderType type) -> nvrhi::ShaderHandle {
  const std::filesystem::path requestedPath(path);
  const std::filesystem::path resolvedPath = requestedPath.is_absolute() ? requestedPath : m_AssetRoot / requestedPath;
  const std::string cacheKey = resolvedPath.lexically_normal().string();
  if (m_shaderCache.contains(cacheKey)) {
    return m_shaderCache[cacheKey];
  }

  ENGINE_LOG_DEBUG("Render", "Shader cache miss | path: {} | shader_type: {}", cacheKey, static_cast<uint32_t>(type));
  auto binary = LoadShaderBinary(cacheKey);
  if (binary.empty()) {
    ENGINE_LOG_ERROR("Render", "Shader binary load failed | path: {} | bytes: 0", cacheKey);
    return nullptr;
  }

  nvrhi::ShaderDesc desc(type);
  desc.debugName = cacheKey;

  auto handle = m_Device->createShader(desc, binary.data(), binary.size());

  if (handle != nullptr) {
    m_shaderCache[cacheKey] = handle;
  } else {
    ENGINE_LOG_ERROR("Render", "GPU shader creation failed | path: {} | shader_type: {} | byte_size: {}", cacheKey,
                     static_cast<uint32_t>(type), binary.size());
  }
  return handle;
}

auto ShaderFactory::LoadShaderBinary(const std::string& path) -> std::vector<char> {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return {};
  }

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<char> buffer(size);
  if (file.read(buffer.data(), size)) {
    return buffer;
  }
  return {};
}
} // namespace Engine::Renderer

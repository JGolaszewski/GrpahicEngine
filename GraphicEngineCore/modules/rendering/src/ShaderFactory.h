#pragma once

#include "runtime/include/CoreExport.h"

#include <filesystem>
#include <nvrhi/nvrhi.h>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Renderer
{

class ENGINE_API ShaderFactory
{
  public:
    ShaderFactory(nvrhi::IDevice *device);

    nvrhi::ShaderHandle GetShader(const std::string &path, nvrhi::ShaderType type);

  private:
    std::vector<char> LoadShaderBinary(const std::string &path);

    std::unordered_map<std::string, nvrhi::ShaderHandle> m_shaderCache;
    nvrhi::IDevice *m_Device;
    std::filesystem::path m_AssetRoot;
};

} // namespace Engine::Renderer

#ifdef _MSC_VER
#pragma warning(pop)
#endif

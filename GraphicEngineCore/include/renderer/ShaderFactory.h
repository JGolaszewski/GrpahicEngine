#pragma once

#include "CoreExport.h"

#include <filesystem>
#include <nvrhi/nvrhi.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace Renderer
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

} // namespace Renderer

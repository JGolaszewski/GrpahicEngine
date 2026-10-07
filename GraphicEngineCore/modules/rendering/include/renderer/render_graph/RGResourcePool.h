#pragma once

#include "runtime/include/CoreExport.h"
#include "rendering/include/renderer/render_graph/RGResources.h"

#include <nvrhi/nvrhi.h>

#include <unordered_map>
#include <vector>

namespace Engine::Renderer
{

class ENGINE_API RGResourcePool
{
  public:
    RGResourcePool(nvrhi::DeviceHandle device);

    nvrhi::TextureHandle Acquire(const RGTextureDesc &desc);
    void Release(const RGTextureDesc &desc, nvrhi::TextureHandle texture);
    void Clear();

  private:
    nvrhi::DeviceHandle m_Device;

    std::unordered_map<RGTextureDesc, std::vector<nvrhi::TextureHandle>, RGTextureDescHasher,
                       RGTextureDescEqual>
        m_FreeTextures;
};

} // namespace Engine::Renderer
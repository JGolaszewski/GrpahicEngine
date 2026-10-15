#pragma once

#include "runtime/include/CoreExport.h"
#include "rendering/src/render_graph/RGResources.h"

#include <nvrhi/nvrhi.h>

#include <unordered_map>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Renderer
{

class ENGINE_API RGResourcePool
{
  public:
    RGResourcePool(nvrhi::DeviceHandle device);

    nvrhi::TextureHandle Acquire(const RGTextureDesc &desc);
    void Release(const RGTextureDesc &desc, const nvrhi::TextureHandle &texture);
    void Clear();

  private:
    nvrhi::DeviceHandle m_Device;

    std::unordered_map<RGTextureDesc, std::vector<nvrhi::TextureHandle>, RGTextureDescHasher,
                       RGTextureDescEqual>
        m_FreeTextures;
};

} // namespace Engine::Renderer

#ifdef _MSC_VER
#pragma warning(pop)
#endif

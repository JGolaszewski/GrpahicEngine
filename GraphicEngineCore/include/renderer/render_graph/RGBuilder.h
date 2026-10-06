#pragma once

#include "CoreExport.h"

#include "renderer/render_graph/RGResources.h"

#include <string>
#include <vector>

namespace Renderer
{
struct RGPassDependencies
{
    std::vector<RGResourceID> inputs;
    std::vector<RGResourceID> outputs;
    std::vector<RGTextureAccess> accesses;
    bool valid = true;
    std::string error;
};

class ENGINE_API RGBuilder
{
  private:
    RGResourceRegistry &m_Registry;
    RGPassDependencies &m_CurrentPassDeps;

  public:
    RGBuilder(RGResourceRegistry &registry, RGPassDependencies &passDeps);

    RGResourceID CreateTexture(const RGTextureDesc &desc);
    RGResourceID ImportTexture(const RGTextureDesc &desc, nvrhi::TextureHandle texture,
                               nvrhi::ResourceStates initialState, nvrhi::ResourceStates finalState);
    RGResourceID CreateAndWriteTexture(const RGTextureDesc &desc);

    void Read(RGResourceID id, nvrhi::ResourceStates state = nvrhi::ResourceStates::ShaderResource,
              nvrhi::TextureSubresourceSet subresources = nvrhi::AllSubresources);
    void Write(RGResourceID id, nvrhi::ResourceStates state = nvrhi::ResourceStates::Unknown,
               nvrhi::TextureSubresourceSet subresources = nvrhi::AllSubresources);
    void ReadWrite(RGResourceID id, nvrhi::ResourceStates state,
                   nvrhi::TextureSubresourceSet subresources = nvrhi::AllSubresources);

  private:
    void DeclareAccess(RGResourceID id, nvrhi::ResourceStates state,
                       nvrhi::TextureSubresourceSet subresources, bool read, bool write);
};
} // namespace Renderer

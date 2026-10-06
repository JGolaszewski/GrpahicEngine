#include "renderer/render_graph/RGResources.h"

#include <utility>

namespace Renderer
{
RGResourceID RGResourceRegistry::CreateResource(const RGTextureDesc &desc)
{
    RGResourceID id;
    id.value = static_cast<uint32_t>(m_Registry.size());
    RGResource resource;
    resource.desc = desc;
    m_Registry.push_back(resource);
    return id;
}

RGResourceID RGResourceRegistry::ImportResource(const RGTextureDesc &desc, nvrhi::TextureHandle texture,
                                                nvrhi::ResourceStates initialState,
                                                nvrhi::ResourceStates finalState)
{
    if (!texture || initialState == nvrhi::ResourceStates::Unknown ||
        finalState == nvrhi::ResourceStates::Unknown)
        return {};
    RGResourceID id = CreateResource(desc);
    RGResource &resource = GetResource(id);
    resource.isImported = true;
    resource.initialState = initialState;
    resource.finalState = finalState;
    resource.physicalHandle = std::move(texture);
    return id;
}

bool RGResourceRegistry::IsValid(RGResourceID id) const noexcept
{
    return id.isValid() && id.value < m_Registry.size();
}
} // namespace Renderer

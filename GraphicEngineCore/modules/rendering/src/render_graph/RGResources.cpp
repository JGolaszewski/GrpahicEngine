#include "rendering/src/render_graph/RGResources.h"

#include <utility>

namespace Engine::Renderer {
auto RGResourceRegistry::CreateResource(const RGTextureDesc& desc) -> RGResourceID {
  RGResourceID id;
  id.value = static_cast<uint32_t>(m_Registry.size());
  RGResource resource;
  resource.desc = desc;
  m_Registry.push_back(resource);
  return id;
}

auto RGResourceRegistry::ImportResource(const RGTextureDesc& desc, nvrhi::TextureHandle texture,
                                        nvrhi::ResourceStates initialState, nvrhi::ResourceStates finalState)
    -> RGResourceID {
  if ((texture == nullptr) || initialState == nvrhi::ResourceStates::Unknown ||
      finalState == nvrhi::ResourceStates::Unknown) {
    return {};
  }
  RGResourceID id = CreateResource(desc);
  RGResource& resource = GetResource(id);
  resource.isImported = true;
  resource.initialState = initialState;
  resource.finalState = finalState;
  resource.physicalHandle = std::move(texture);
  return id;
}

auto RGResourceRegistry::IsValid(RGResourceID id) const noexcept -> bool {
  return id.isValid() && id.value < m_Registry.size();
}
} // namespace Engine::Renderer

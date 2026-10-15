#include "rendering/src/render_graph/RenderContext.h"
#include "rendering/src/render_graph/FrameContext.h"

namespace Engine::Renderer {

RenderContext::RenderContext(nvrhi::IDevice* device, RGResourceRegistry* registry, FrameContext* frameContext, float dt)
    : m_Device(device), m_Registry(registry),
      m_Blackboard((frameContext != nullptr) ? &frameContext->GetBlackboard() : nullptr), m_FrameContext(frameContext),
      m_DeltaTime(dt) {}

auto RenderContext::GetPhysicalTexture(RGResourceID id) const -> nvrhi::TextureHandle {
  if ((m_Registry == nullptr) || !m_Registry->IsValid(id)) {
    return nullptr;
  }
  return m_Registry->GetResource(id).physicalHandle;
}
} // namespace Engine::Renderer

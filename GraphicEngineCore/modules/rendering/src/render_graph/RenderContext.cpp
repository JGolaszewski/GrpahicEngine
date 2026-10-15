#include "rendering/src/render_graph/RenderContext.h"
#include "rendering/src/render_graph/FrameContext.h"

namespace Engine::Renderer
{

RenderContext::RenderContext(nvrhi::IDevice *device, RGResourceRegistry *registry, FrameContext *frameContext,
                             float dt)
    : m_Device(device), m_Registry(registry),
      m_Blackboard(frameContext ? &frameContext->GetBlackboard() : nullptr), m_FrameContext(frameContext),
      m_DeltaTime(dt)
{
}

nvrhi::TextureHandle RenderContext::GetPhysicalTexture(RGResourceID id) const
{
    if (!m_Registry || !m_Registry->IsValid(id))
        return nullptr;
    return m_Registry->GetResource(id).physicalHandle;
}
} // namespace Engine::Renderer

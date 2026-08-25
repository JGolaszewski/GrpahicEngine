#include "renderer/render_graph/RenderContext.h"

namespace Renderer {

	RenderContext::RenderContext(nvrhi::IDevice* device, RGResourceRegistry* registry, FrameBlackboard* blackboard, float dt):
		m_Device(device), m_Registry(registry), m_Blackboard(blackboard), m_DeltaTime(dt) {}


	nvrhi::TextureHandle RenderContext::GetPhysicalTexture(RGResourceID id) const
	{
		if (!m_Registry || !m_Registry->IsValid(id)) return nullptr;
		return m_Registry->GetResource(id).physicalHandle;
	}
}

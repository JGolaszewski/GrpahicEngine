#include "renderer/render_graph/RenderContext.h"

namespace Renderer {

	RenderContext::RenderContext(nvrhi::IDevice* device, RGResourceRegistry* registry, FrameBlackboard* blackboard, float dt):
		m_Device(device), m_Registry(registry), m_Blackboard(blackboard), m_DeltaTime(dt) {}


	nvrhi::TextureHandle RenderContext::GetPhysicalTexture(RGResourceID id) const
	{
		return m_Registry->GetResource(id).physicalHandle;
	}
}

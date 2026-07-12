#include "RednderContext.h"

namespace Renderer {

	RenderContext::RenderContext(const RGResourceRegistry* registry, uint32_t w, uint32_t h, float dt):
		m_Registry(registry), m_FrameWidth(w), m_FrameHeight(h), m_DeltaTime(dt) {}


	nvrhi::TextureHandle RenderContext::GetPhysicalTexture(RGResourceID id) const
	{
		return m_Registry->GetResource(id);
	}
}
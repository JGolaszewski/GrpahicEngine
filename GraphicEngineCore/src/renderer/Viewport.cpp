#include "renderer/Viewport.h"


namespace Engine {
	Engine::Viewport::Viewport(uint32_t width, uint32_t height, nvrhi::IFramebuffer* framebuffer)
		: m_Width(width), m_Height(height), m_Framebuffer(framebuffer) {
	}

	float Viewport::GetAspectRatio() const
	{
		if (m_Width == 0 || m_Height == 0) {
			return 1.0f;
		}

		return static_cast<float>(m_Width) / static_cast<float>(m_Height);
	}

}



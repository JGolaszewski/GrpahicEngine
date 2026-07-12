#include "Viewport.h"


namespace Engine {
	Engine::Viewport::Viewport(uint32_t width, uint32_t height, nvrhi::IFramebuffer* framebuffer)
		: m_Width(width), m_Height(height), m_Framebuffer(framebuffer) {
	}
}



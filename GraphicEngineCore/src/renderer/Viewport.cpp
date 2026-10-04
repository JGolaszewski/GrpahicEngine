#include "renderer/Viewport.h"

#include "ViewportTarget.h"

#include <utility>


namespace Engine {
	Engine::Viewport::Viewport(uint32_t width, uint32_t height, nvrhi::IFramebuffer* framebuffer)
		: m_Width(width), m_Height(height), m_Framebuffer(framebuffer) {
	}

	Viewport::Viewport(std::shared_ptr<ViewportTarget> target)
		: m_Width(target ? target->GetWidth() : 0)
		, m_Height(target ? target->GetHeight() : 0)
		, m_Framebuffer(target
			? static_cast<nvrhi::IFramebuffer*>(target->GetFramebufferHandle())
			: nullptr)
		, m_Target(std::move(target)) {
	}

	float Viewport::GetAspectRatio() const
	{
		if (m_Width == 0 || m_Height == 0) {
			return 1.0f;
		}

		return static_cast<float>(m_Width) / static_cast<float>(m_Height);
	}

}



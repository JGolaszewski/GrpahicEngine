#pragma once

#include "CoreExport.h"

#include "Scene.h"
#include <nvrhi/nvrhi.h>
#include <memory>

namespace Renderer {
	class Camera;
}

namespace Engine {

	class ENGINE_API Viewport {
	public:
		Viewport(uint32_t width, uint32_t height, nvrhi::IFramebuffer* framebuffer);

		inline uint32_t GetWidth() const { return m_Width; }
		inline uint32_t GetHeight() const { return m_Height; }
		float GetAspectRatio() const;
		nvrhi::IFramebuffer* GetFramebuffer() const { return m_Framebuffer; }

	private:
		uint32_t m_Width;
		uint32_t m_Height;
		nvrhi::IFramebuffer* m_Framebuffer;
	};

}
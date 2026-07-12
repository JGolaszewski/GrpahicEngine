#pragma once

#include <memory>

namespace Engine {

	class ENGINE_API Viewport {
	public:
		Viewport(uint32_t width, uint32_t height, nvrhi::IFramebuffer* framebuffer);

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		nvrhi::IFramebuffer* GetFramebuffer() const { return m_Framebuffer; }

		inline void SetTargetScene(std::shared_ptr<Scene> scene) { m_TargetScene = scene; }
		inline std::shared_ptr<Scene> GetTargetScene() const { return m_TargetScene; }

		inline void SetCamera(Renderer::Camera* camera) { m_Camera = camera; }
		inline Camera* GetCamera() const { return m_Camera; }

	private:
		uint32_t m_Width;
		uint32_t m_Height;
		nvrhi::IFramebuffer* m_Framebuffer;
		
		Renderer::Camera* m_Camera;
		std::shared_ptr<Scene> m_TargetScene;
	};

}
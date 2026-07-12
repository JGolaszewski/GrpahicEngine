#pragma once 

namespace Renderer {

	class RenderContext {
	public:
		RenderContext(const RGResourceRegistry* registry, uint32_t w, uint32_t h, float dt);

		nvrhi::TextureHandle GetPhysicalTexture(RGResourceID id) const;

		inline uint32_t GetFrameWidth() const { return m_FrameWidth; }
		inline uint32_t GetFrameHeight() const { return m_FrameHeight; }
		inline float GetDeltaTime() const { return m_DeltaTime; }
	
	private:
		const RGResourceRegistry* m_Registry;

		uint32_t m_FrameWidth;
		uint32_t m_FrameHeight;
		float m_DeltaTime;
	};
}
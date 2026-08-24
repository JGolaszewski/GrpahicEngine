#pragma once

#include <string>
#include <vector>
#include "CoreExport.h"
#include "RGBuilder.h"
#include "renderer/RenderPacket.h"
#include "renderer/Light.h"
#include "RenderContext.h"

namespace Engine {
	class SceneRenderer;
}

namespace Renderer {

	class FrameBlackboard;
	class RenderContext;

	class ENGINE_API IRenderPass {
	public:
		virtual ~IRenderPass() = default;

		virtual std::string GetName() const = 0;
		virtual bool HasSideEffect() const { return false; }
		virtual void Setup(RGBuilder& builder, FrameBlackboard& blackboard) = 0;
		virtual void Execute(nvrhi::ICommandList* cmd, const RenderContext& ctx) = 0;
		virtual RGResourceID GetOutputID() const { return RGResourceID(); }
	};

	class ENGINE_API ShadowPass : public IRenderPass {
	public:
		ShadowPass();

		std::string GetName() const override { return "ShadowPass"; }

		void Setup(Renderer::RGBuilder& builder, Renderer::FrameBlackboard& blackboard) override;
		void Execute(nvrhi::ICommandList* cmd, const Renderer::RenderContext& ctx) override;
		RGResourceID GetOutputID() const override { return m_ShadowMapID; }
	
	private:
		RGResourceID m_ShadowMapID;
		nvrhi::TextureHandle m_ShadowMapTexture;
		std::vector<nvrhi::FramebufferHandle> m_ShadowFramebuffers;
	};

	class ENGINE_API ForwardPass : public Renderer::IRenderPass {
	private:
		Renderer::RGResourceID m_ShadowMapInputID;

	public:
		ForwardPass();

		std::string GetName() const override { return "ForwardPass"; }
		bool HasSideEffect() const override { return true; }

		void Setup(Renderer::RGBuilder& builder, Renderer::FrameBlackboard& blackboard) override;
		void Execute(nvrhi::ICommandList* cmd, const Renderer::RenderContext& ctx) override;
	};
}

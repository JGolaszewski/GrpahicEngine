#pragma once

#include "CoreExport.h"

#include "RGBuilder.h"
#include "RenderPacket.h"
#include "Light.h"

namespace Renderer {
	class ENGINE_API IRenderPass {
		virtual ~IRenderPass() = default;

		virtual std::string GetName() const = 0;
		virtual bool HasSideEffect() const { return false; }
		virtual void Setup(RGBuilder& builder) = 0;
		virtual void Execute(nvrhi::ICommandList* cmd, const RenderContext& ctx) = 0;
		virtual RGResourceID GetOutputID() const { return RGResourceID(); }
	};

	struct ShadowDrawData {
		DirectionalLightData Light;
		std::vector<RenderPacket> Packets;
	};

	class ENGINE_API ShadowPass : public IRenderPass {
	public:
		ShadowPass(std::vector<ShadowDrawData> shadowData, PipelineCache* pipelineCache);

		std::string GetName() const override { return "ShadowPass"; }

		void Setup(Renderer::RGBuilder& builder) override;
		void Execute(nvrhi::ICommandList* cmd, const Renderer::RenderContext& ctx) override;
		RGResourceID GetOutputID() const override { return m_ShadowMapID; }
	
	private:
		PipelineCache* m_PipelineCache;
		RGResourceID m_ShadowMapID;
		std::vector<ShadowDrawData> m_ShadowData;
	};

	class ForwardPass : public Renderer::IRenderPass {
	private:
		Renderer::RGResourceID m_ShadowMapInputID;
		Engine::SceneRenderer* m_SceneRenderer;
		Renderer::Viewport* m_Viewport;

	public:
		ForwardPass(Renderer::RGResourceID shadowMap,
			Renderer::Viewport* vp,
			std::vector<DirectionalLightData> lights,
			std::vector<RenderPacket> packets);

		std::string GetName() const override { return "ForwardPass"; }
		bool HasSideEffect() const override { return true; }

		void Setup(Renderer::RGBuilder& builder) override;
		void Execute(nvrhi::ICommandList* cmd, const Renderer::RenderContext& ctx) override;
	};
}
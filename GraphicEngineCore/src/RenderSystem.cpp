#include "renderer/RenderSystem.h"

#include "renderer/SceneExtractor.h"

namespace Engine {
	
	RenderSystem::RenderSystem(IRenderBackend* backend)
		: m_RenderBackend(backend) {
		m_ShaderFactory = std::make_unique<Renderer::ShaderFactory>(m_RenderBackend->GetDevice());
		auto m_DefaultVS = m_ShaderFactory->GetShader("shaders/color_vs.dxbc", nvrhi::ShaderType::Vertex);
		auto m_DefaultPS = m_ShaderFactory->GetShader("shaders/color_ps.dxbc", nvrhi::ShaderType::Pixel);

		m_MaterialSystem = std::make_unique<MaterialSystem>(m_RenderBackend->GetDevice(), m_DefaultVS, m_DefaultPS);
		m_SceneRenderer = std::make_unique<SceneRenderer>(m_RenderBackend->GetDevice());
		m_SceneRenderer->Init(m_RenderBackend->GetDevice());

		m_CommandList = m_RenderBackend->GetDevice()->createCommandList();
	}

	void RenderSystem::SubmitViewport(Renderer::Viewport* viewport) {
		std::shared_ptr<Scene> scene = viewport->GetTargetScene();
		
		if (!scene || !viewport) return;

		std::vector<RenderPacket> mainPackets;
		SceneExtractor::ExtractView(scene.get(), viewport->GetCamera(), mainPackets);

		std::vector<std::vector<Renderer::RenderPacket>> shadowsPackets;
		for (const auto& light : scene->GetLights()) {
			shadowsPackets.push_back(
				SceneExtractor::ExtractDirectionalLight(scene, light->GetDirectionalLightData())
			);
		}

		auto shadowPass = std::make_unique<ShadowPass>(m_SceneRenderer.get());
		Renderer::RGResourceID shadowOutput = shadowPass->GetOutputID();

		auto forwardPass = std::make_unique<ForwardPass>(m_SceneRenderer.get(), shadowOutput, viewport);

		m_RenderGraph->AddPass(std::move(shadowPass));
		m_RenderGraph->AddPass(std::move(forwardPass));
	}

	void RenderSystem::BeginFrame() {
		m_CommandList->open();
		ClearFrameBuffer();
	}

	void RenderSystem::EndFrame() {
		m_RenderGraph->Compile();
		m_RenderGraph->Execute(m_CommandList);


		m_CommandList->close();
		m_RenderBackend->GetDevice()->executeCommandList(m_CommandList);
		m_RenderBackend->Present();

		m_SceneRenderer->ClearQueue();
	}

	void RenderSystem::ClearFrameBuffer()
	{
		auto* frameBuffer = m_RenderBackend->GetDefaultFramebuffer();

		if (!frameBuffer) {
			return;
		}	

		nvrhi::ITexture* colorTex = frameBuffer->getDesc().colorAttachments[0].texture;
		if (colorTex) {
			cmd->clearTextureFloat(colorTex, nvrhi::AllSubresources, nvrhi::Color(0.1f, 0.1f, 0.1f, 1.0f));
		}

		nvrhi::ITexture* depthTex = frameBuffer->getDesc().depthAttachment.texture;
		if (depthTex) {
			cmd->clearDepthStencilTexture(depthTex, nvrhi::AllSubresources, true, 1.0f, false, 0);
		}
	}

}
#include "RenderSystem.h"
#include "renderer/render_graph/RenderPass.h"

namespace Renderer {
	class Camera;
}

namespace Engine {

	RenderSystem::RenderSystem(IRenderBackend* backend)
		: m_RenderBackend(backend)
		, m_CommandList(backend->GetDevice()->createCommandList()) {

		m_ShaderFactory = std::make_unique<Renderer::ShaderFactory>(m_RenderBackend->GetDevice());
		auto m_DefaultVS = m_ShaderFactory->GetShader("shaders/color_vs.dxbc", nvrhi::ShaderType::Vertex);
		auto m_DefaultPS = m_ShaderFactory->GetShader("shaders/color_ps.dxbc", nvrhi::ShaderType::Pixel);

		m_MaterialSystem = std::make_unique<Renderer::MaterialSystem>(m_RenderBackend->GetDevice(), m_DefaultVS, m_DefaultPS);
		m_SceneRenderer = std::make_unique<SceneRenderer>(m_RenderBackend->GetDevice());
		m_SceneRenderer->Init(m_RenderBackend->GetDevice(), m_MaterialSystem.get());

		m_RenderGraph = std::make_unique<Renderer::RenderGraph>(m_RenderBackend->GetDevice());
	}

	void RenderSystem::ExecuteImmediate(std::function<void(nvrhi::ICommandList*)> fn) {
		nvrhi::CommandListHandle cmd = m_RenderBackend->GetDevice()->createCommandList();
		cmd->open();
		fn(cmd);
		cmd->close();
		m_RenderBackend->GetDevice()->executeCommandList(cmd);
	}

	void RenderSystem::BeginFrame() {
		m_CommandList->open();
		ClearFrameBuffer();

		m_ViewPool.clear();

		m_RenderGraph->Reset();
		m_RenderGraph->GetBlackboard().Clear();
	}

	void RenderSystem::SubmitViews(const std::vector<Engine::ViewportBinding>& views) {
		Renderer::FrameBlackboard& bb = m_RenderGraph->GetBlackboard();

		std::vector<Renderer::RenderView*> sceneViews;
		std::vector<Renderer::RenderView*> shadowViews;

		for (uint32_t i = 0; i < SceneRenderer::MaxShadowedLights; ++i) {
			auto view = std::make_unique<Renderer::RenderView>();
			view->ViewType = Renderer::RenderView::Type::Shadow;

			shadowViews.push_back(view.get());
			m_ViewPool.push_back(std::move(view));
		}
		bb.SetShadowViews(shadowViews);

		Engine::Scene* activeScene = nullptr;

		for (auto binding : views) {
			if (!binding.IsValid()) continue;

			auto view = std::make_unique<Renderer::RenderView>();
			view->ViewType = Renderer::RenderView::Type::Main;
			view->Initialize(binding.SourceCamera, binding.TargetViewport);
			view->TargetScene = binding.TargetScene;

			sceneViews.push_back(view.get());
			m_ViewPool.push_back(std::move(view));

			if (!activeScene) {
				activeScene = binding.TargetScene;
			}
		}
		bb.SetSceneViews(sceneViews);

		if (activeScene) {
			m_SceneRenderer->BuildShadowData(activeScene, bb);
			m_SceneRenderer->BuildViews(bb);
			m_SceneRenderer->BuildLighting(activeScene, bb);
		}

		bb.SetLightBuffer(m_SceneRenderer->GetLightBuffer());
		bb.SetShadowSampler(m_SceneRenderer->GetShadowSampler());
		bb.SetPerObjectBindingLayout(m_SceneRenderer->GetPerObjectBindingLayout());
		bb.SetSceneBindingLayout(m_SceneRenderer->GetSceneBindingLayout());

		m_RenderGraph->AddPass(std::make_unique<Renderer::ShadowPass>());
		m_RenderGraph->AddPass(std::make_unique<Renderer::ForwardPass>());
	}

	void RenderSystem::EndFrame() {
		m_RenderGraph->Compile();
		m_RenderGraph->Execute(m_CommandList);

		m_CommandList->close();
		m_RenderBackend->GetDevice()->executeCommandList(m_CommandList);

		m_SceneRenderer->Clear();
	}

	void RenderSystem::ClearFrameBuffer() {
		auto* frameBuffer = m_RenderBackend->GetDefaultFramebuffer();

		if (!frameBuffer) {
			return;
		}

		nvrhi::ITexture* colorTex = frameBuffer->getDesc().colorAttachments[0].texture;
		if (colorTex) {
			m_CommandList->clearTextureFloat(colorTex, nvrhi::AllSubresources, nvrhi::Color(0.1f, 0.1f, 0.1f, 1.0f));
		}

		nvrhi::ITexture* depthTex = frameBuffer->getDesc().depthAttachment.texture;
		if (depthTex) {
			m_CommandList->clearDepthStencilTexture(depthTex, nvrhi::AllSubresources, true, 1.0f, false, 0);
		}
	}
}
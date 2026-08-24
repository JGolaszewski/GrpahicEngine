#pragma once
#include "CoreExport.h"
#include "RenderBackend.h"
#include "renderer/RenderPacket.h"
#include "SceneRenderer.h"
#include "renderer/Viewport.h"
#include "renderer/MaterialSystem.h"
#include "renderer/ShaderFactory.h"
#include "renderer/render_graph/RenderGraph.h"
#include "renderer/ViewportBinding.h"
#include <memory>
#include <functional>
#include <vector>

namespace Engine {
	class ENGINE_API RenderSystem {
	public:
		RenderSystem(IRenderBackend* backend);

		void BeginFrame();
		void SubmitViews(const std::vector<Engine::ViewportBinding>& views);
		void EndFrame();

		void ExecuteImmediate(std::function<void(nvrhi::ICommandList*)> fn);

		SceneRenderer* GetSceneRenderer() const { return m_SceneRenderer.get(); }
		Renderer::MaterialSystem* GetMaterialSystem() const { return m_MaterialSystem.get(); }
		Renderer::ShaderFactory* GetShaderFactory() const { return m_ShaderFactory.get(); }
		Renderer::RenderGraph* GetRenderGraph() const { return m_RenderGraph.get(); }
		IRenderBackend* GetRenderBackend() const { return m_RenderBackend; }

	private:
		void ClearFrameBuffer();

		IRenderBackend* const m_RenderBackend;
		nvrhi::ICommandList* m_CommandList;

		std::unique_ptr<Renderer::RenderGraph> m_RenderGraph;

		std::unique_ptr<SceneRenderer> m_SceneRenderer;
		std::unique_ptr<Renderer::MaterialSystem> m_MaterialSystem;
		std::unique_ptr<Renderer::ShaderFactory> m_ShaderFactory;

		std::vector<std::unique_ptr<Renderer::RenderView>> m_ViewPool;
	};
}
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
#include "ViewportTarget.h"
#include "PrimitiveFactory.h"
#include <memory>
#include <functional>
#include <optional>
#include <vector>

namespace Engine {
	class ENGINE_API RenderSystem {
	public:
		RenderSystem(IRenderBackend* backend);
		bool IsValid() const { return m_CommandList != nullptr; }

		void BeginFrame();
		void SubmitViews(const std::vector<Engine::ViewportBinding>& views);
		void EndFrame();
		std::optional<Engine::LightId> PickLight(float viewportX, float viewportY) const;
		std::shared_ptr<Engine::ViewportTarget> CreateViewportTarget(uint32_t width, uint32_t height);
		std::shared_ptr<Renderer::Entity> CreatePrimitive(
			Engine::PrimitiveShape shape,
			std::shared_ptr<Renderer::Material> material,
			const DirectX::XMFLOAT3& position,
			const DirectX::XMFLOAT3& scale = { 1.0f, 1.0f, 1.0f });

		void ExecuteImmediate(std::function<void(nvrhi::ICommandList*)> fn);

		SceneRenderer* GetSceneRenderer() const { return m_SceneRenderer.get(); }
		Renderer::MaterialSystem* GetMaterialSystem() const { return m_MaterialSystem.get(); }
		Renderer::ShaderFactory* GetShaderFactory() const { return m_ShaderFactory.get(); }
		Renderer::RenderGraph* GetRenderGraph() const { return m_RenderGraph.get(); }
		Engine::PrimitiveFactory* GetPrimitiveFactory() const { return m_PrimitiveFactory.get(); }
		IRenderBackend* GetRenderBackend() const { return m_RenderBackend; }

	private:
		void ClearFrameBuffer();

		IRenderBackend* const m_RenderBackend;
		nvrhi::CommandListHandle m_CommandList;
		bool m_FrameActive = false;

		std::unique_ptr<Renderer::RenderGraph> m_RenderGraph;

		std::unique_ptr<SceneRenderer> m_SceneRenderer;
		std::unique_ptr<Renderer::MaterialSystem> m_MaterialSystem;
		std::unique_ptr<Renderer::ShaderFactory> m_ShaderFactory;
		std::unique_ptr<Engine::PrimitiveFactory> m_PrimitiveFactory;

		std::vector<std::unique_ptr<Renderer::RenderView>> m_ViewPool;
	};
}

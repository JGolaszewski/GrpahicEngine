#pragma once

#include "CoreExport.h"

#include "RenderBackend.h"
#include "RenderPacket.h"
#include "SceneRenderer.h"
#include "Viewport.h"

#include "renderer/MaterialSystem.h"

#include <memory>

namespace Engine {
	class ENGINE_API RenderSystem {
		RenderSystem(IRenderBackend* backend);

		void SubmitViewport(Renderer::Viewport* viewport);
		void BeginFrame();
		void EndFrame();
		
	private:
		void ClearFrameBuffer();

		IRenderBackend* const m_RenderBackend;
		nvrhi::ICommandList* const m_CommandList;

		Renderer::RenderGraph m_RenderGraph;

		std::unique_ptr<SceneRenderer> m_SceneRenderer;
		std::unique_ptr<MaterialSystem> m_MaterialSystem;
		std::unique_ptr<Renderer::ShaderFactory> m_ShaderFactory;
	};
}
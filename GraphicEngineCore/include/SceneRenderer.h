#pragma once

#include "CoreExport.h"
#include <DirectXMath.h>
#include <vector>
#include <nvrhi/nvrhi.h>

#include "renderer/Entity.h" 
#include "PipelineCache.h"

namespace Engine {

	class ENGINE_API SceneRenderer {
	public:
		void Init(nvrhi::IDevice* device);

		void Submit(Renderer::Entity* entity);

		void RenderAll(nvrhi::ICommandList* cmd, nvrhi::IFramebuffer* fb, const DirectX::XMMATRIX& viewProj);

		nvrhi::BindingLayoutHandle GetPerObjectBindingLayout() const;

	private:
		PipelineCache m_PipelineCache;

		std::vector<Renderer::Entity*> m_CommandQueue;

		nvrhi::BufferHandle m_PerObjectBuffer;
		nvrhi::BindingLayoutHandle m_PerObjectBindingLayout;
		nvrhi::BindingSetHandle m_PerObjectBindingSet;
	};

}
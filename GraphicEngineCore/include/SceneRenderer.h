#pragma once

#include "CoreExport.h"
#include <DirectXMath.h>
#include <vector>
#include <nvrhi/nvrhi.h>

#include "renderer/Entity.h" 
#include "renderer/Light.h"
#include "PipelineCache.h"
#include <array>

namespace Engine {

	class ENGINE_API SceneRenderer {
	public:
      static constexpr uint32_t MaxLights = 128;
	  static constexpr uint32_t MaxShadowedLights = 16;
	  static constexpr uint32_t ShadowMapSize = 1024;

		void Init(nvrhi::IDevice* device);

		void Submit(Renderer::Entity* entity);

		void SetDirectionalLight(const Renderer::SpotLight& light);
		void AddLight(const Renderer::SpotLight& light);
		void ClearLights();

		void RenderAll(nvrhi::ICommandList* cmd, nvrhi::IFramebuffer* fb, const DirectX::XMMATRIX& viewProj);

		nvrhi::BindingLayoutHandle GetPerObjectBindingLayout() const;

	private:
		PipelineCache m_PipelineCache;

		std::vector<Renderer::Entity*> m_CommandQueue;

		nvrhi::BufferHandle m_PerObjectBuffer;
        nvrhi::BufferHandle m_LightBuffer;
		nvrhi::BindingLayoutHandle m_PerObjectBindingLayout;
		nvrhi::BindingSetHandle m_PerObjectBindingSet;
		nvrhi::TextureHandle m_ShadowMapArray;
		nvrhi::SamplerHandle m_ShadowSampler;
		std::vector<nvrhi::FramebufferHandle> m_ShadowFramebuffers;

      std::vector<Renderer::DirectionalLightData> m_Lights;
	};

}
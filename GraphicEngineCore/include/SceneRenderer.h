#pragma once

#include "CoreExport.h"
#include <DirectXMath.h>
#include <vector>
#include <nvrhi/nvrhi.h>

#include "renderer/Entity.h" 
#include "renderer/Light.h"
#include "PipelineCache.h"
#include "MaterialSystem.h"
#include "RenderView.h"
#include "Light.h"
#include <array>

namespace Engine {

	class ENGINE_API SceneRenderer {
	public:
		void Init(nvrhi::IDevice* device, MaterialSystem materialSystem);

		static void MainView(const Camera& camera, const Scene& scene, Renderer::RederView& outView);
		static void ExtractShadowView(const Renderer::DirectionalLight& lightm const Scene& scene, Renderer::RederView& outView);
	
	private:
		RenderPacket CreateRenderPacket(const Entity* entity, nvrhi::GraphicsPipelineHandle pso);
		
		PipelineCache m_PipelineCache;

		std::vector<Renderer::Entity*> m_CommandQueue;

		nvrhi::DeviceHandle m_Device;
		nvrhi::BufferHandle m_PerObjectBuffer;
        nvrhi::BufferHandle m_LightBuffer;
		nvrhi::BindingLayoutHandle m_PerObjectBindingLayout;
		nvrhi::BindingSetHandle m_PerObjectBindingSet;
		nvrhi::TextureHandle m_ShadowMapArray;
		nvrhi::SamplerHandle m_ShadowSampler;
		
		std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_PerObjectBindingSetsCache;
		std::vector<nvrhi::FramebufferHandle> m_ShadowFramebuffers;
		std::vector<Renderer::DirectionalLightData> m_Lights;
		MaterialSystem m_MaterialSystem;
	};

}
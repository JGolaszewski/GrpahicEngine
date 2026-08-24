#pragma once

#include "CoreExport.h"
#include <DirectXMath.h>
#include <vector>
#include <unordered_map>
#include <nvrhi/nvrhi.h>

#include "renderer/Entity.h"
#include "renderer/Light.h"
#include "PipelineCache.h"
#include "renderer/MaterialSystem.h"
#include "renderer/render_graph/RenderView.h"
#include "Scene.h"
#include "renderer/Camera.h"
#include "renderer/RenderPacket.h"
#include "renderer/render_graph/FrameBlackboard.h"

namespace Engine {

	class ENGINE_API SceneRenderer {
	public:
		static constexpr uint32_t ShadowMapSize = 2048;
		static constexpr uint32_t MaxLights = ENGINE_MAX_LIGHTS;
		static constexpr uint32_t MaxShadowedLights = 4;
		static constexpr uint32_t PerObjectDataStride = 256;

		SceneRenderer(nvrhi::IDevice* device);
		void Init(nvrhi::IDevice* device, Renderer::MaterialSystem* materialSystem);
		
		void BuildViews(Renderer::FrameBlackboard& blackboard, nvrhi::ICommandList* commandList);
		void BuildLighting(Engine::Scene* scene, Renderer::FrameBlackboard& blackboard);
		void BuildShadowData(Engine::Scene* scene, Renderer::FrameBlackboard& blackboard);

		inline nvrhi::BufferHandle GetLightBuffer() const { return m_LightBuffer; }
		inline nvrhi::SamplerHandle GetShadowSampler() const { return m_ShadowSampler; }
		inline nvrhi::BindingLayoutHandle GetPerObjectBindingLayout() const { return m_PerObjectBindingLayout; }
		inline nvrhi::BindingLayoutHandle GetSceneBindingLayout() const { return m_SceneBindingLayout; }
		inline Renderer::MaterialSystem* GetMaterialSystem() const { return m_MaterialSystem; }
		inline PipelineCache* GetPipelineCache() { return &m_PipelineCache; }

	private:

		void CullAndFillView(Renderer::RenderView* view, nvrhi::ICommandList* commandList);
		Renderer::RenderPacket CreateRenderPacket(const Renderer::Entity* entity, nvrhi::BufferHandle perObjectBuffer);

		PipelineCache m_PipelineCache;

		nvrhi::DeviceHandle m_Device;
		nvrhi::BufferHandle m_LightBuffer;
		nvrhi::BindingLayoutHandle m_PerObjectBindingLayout;
		nvrhi::BindingLayoutHandle m_SceneBindingLayout;
		nvrhi::SamplerHandle m_ShadowSampler;

		Renderer::MaterialSystem* m_MaterialSystem = nullptr;

	};

}

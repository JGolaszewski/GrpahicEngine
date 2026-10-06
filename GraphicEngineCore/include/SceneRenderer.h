#pragma once

#include "CoreExport.h"
#include <DirectXMath.h>
#include <vector>
#include <unordered_map>
#include <nvrhi/nvrhi.h>
#include <optional>

#include "renderer/Entity.h"
#include "renderer/Light.h"
#include "renderer/DebugGeometry.h"
#include "PipelineCache.h"
#include "renderer/MaterialSystem.h"
#include "renderer/ShaderFactory.h"
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

		struct SubmissionStats {
			uint32_t ObjectsSubmitted = 0;
			uint32_t ObjectsCulled = 0;
			uint32_t PerObjectBytesUsed = 0;
		};

		SceneRenderer(nvrhi::IDevice* device);
		void Init(nvrhi::IDevice* device,
			Renderer::MaterialSystem* materialSystem,
			Renderer::ShaderFactory* shaderFactory);
		
		void BuildViews(Renderer::FrameBlackboard& blackboard, nvrhi::ICommandList* commandList);
		void BuildDebugGeometry(Renderer::RenderView* view);
		void RenderDebugGeometry(nvrhi::ICommandList* commandList, const Renderer::RenderView& view);
		std::optional<Engine::LightId> PickLight(const Renderer::RenderView& view,
			float viewportX,
			float viewportY) const;
		void BuildLighting(Engine::Scene* scene, const std::vector<Renderer::RenderView*>& shadowViews,
			std::vector<Renderer::DirectionalLightData>& lights);
		void BuildShadowData(Engine::Scene* scene, const std::vector<Renderer::RenderView*>& shadowViews,
			uint32_t firstShadowSlot);

		inline nvrhi::BufferHandle GetLightBuffer() const { return m_LightBuffer; }
		inline nvrhi::SamplerHandle GetShadowSampler() const { return m_ShadowSampler; }
		inline nvrhi::BindingLayoutHandle GetPerObjectBindingLayout() const { return m_PerObjectBindingLayout; }
		inline nvrhi::BindingLayoutHandle GetSceneBindingLayout() const { return m_SceneBindingLayout; }
		inline Renderer::MaterialSystem* GetMaterialSystem() const { return m_MaterialSystem; }
		inline PipelineCache* GetPipelineCache() { return &m_PipelineCache; }
		inline const SubmissionStats& GetSubmissionStats() const { return m_SubmissionStats; }

	private:

		void CullAndFillView(Renderer::RenderView* view, nvrhi::ICommandList* commandList, uint32_t viewIndex);
		void SortViewQueues(Renderer::RenderView* view);
		void EnsureDebugBuffer(nvrhi::BufferHandle& buffer,
			uint64_t& capacity,
			size_t vertexCount,
			const char* debugName);
		nvrhi::GraphicsPipelineHandle GetDebugPipeline(const nvrhi::FramebufferInfo& framebuffer,
			nvrhi::PrimitiveType primitiveType,
			Renderer::DebugDepthMode depthMode);
		Renderer::RenderPacket CreateRenderPacket(
			const Renderer::Mesh* mesh,
			const Renderer::Material* material,
			nvrhi::BufferHandle perObjectBuffer);

		PipelineCache m_PipelineCache;

		nvrhi::DeviceHandle m_Device;
		nvrhi::BufferHandle m_LightBuffer;
		nvrhi::BindingLayoutHandle m_PerObjectBindingLayout;
		nvrhi::BindingLayoutHandle m_SceneBindingLayout;
		nvrhi::SamplerHandle m_ShadowSampler;
		nvrhi::ShaderHandle m_DebugVS;
		nvrhi::ShaderHandle m_DebugPS;
		nvrhi::BindingLayoutHandle m_DebugBindingLayout;
		nvrhi::BufferHandle m_DebugViewBuffer;
		nvrhi::BufferHandle m_DebugLineBuffer;
		nvrhi::BufferHandle m_DebugTriangleBuffer;
		uint64_t m_DebugLineBufferCapacity = 0;
		uint64_t m_DebugTriangleBufferCapacity = 0;

		Renderer::MaterialSystem* m_MaterialSystem = nullptr;
		SubmissionStats m_SubmissionStats;

	};

}

#pragma once

#include <nvrhi/nvrhi.h>
#include <unordered_map>
#include <vector>
#include "renderer/Mesh.h" 

namespace Engine {

	struct PipelineKey {
		nvrhi::ShaderHandle VS;
		nvrhi::ShaderHandle PS;
		nvrhi::BindingLayoutHandle PerObjectLayout;
		nvrhi::BindingLayoutHandle SceneLayout;
		nvrhi::BindingLayoutHandle MaterialLayout;
		bool Wireframe = false;
		bool DepthTest = true;
		bool DepthWrite = true;
		nvrhi::RasterCullMode CullMode = nvrhi::RasterCullMode::Back;

		nvrhi::FramebufferInfo Framebuffer;
		nvrhi::PrimitiveType PrimType = nvrhi::PrimitiveType::TriangleList;
		bool PrimitiveRestart = false;
		std::vector<nvrhi::VertexAttributeDesc> InputLayout;
		nvrhi::BlendState BlendState;
		nvrhi::DepthStencilState DepthStencilState;
		nvrhi::RasterState RasterState;
		nvrhi::SinglePassStereoState SinglePassStereoState;

		bool operator==(const PipelineKey& other) const;
	};

	struct PipelineKeyHasher {
		std::size_t operator()(const PipelineKey& k) const;
	};

	class PipelineCache {
	public:
		void Init(nvrhi::IDevice* device);

		nvrhi::GraphicsPipelineHandle GetOrCreatePipeline(const PipelineKey& key, const nvrhi::FramebufferInfo& fbInfo);

	private:
		nvrhi::DeviceHandle m_Device;
		std::unordered_map<PipelineKey, nvrhi::GraphicsPipelineHandle, PipelineKeyHasher> m_Cache;
	};
}

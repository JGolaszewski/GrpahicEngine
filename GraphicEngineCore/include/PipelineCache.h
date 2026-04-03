#pragma once

#include <nvrhi/nvrhi.h>
#include <unordered_map>
#include "renderer/Mesh.h" 

namespace Engine {

	struct PipelineKey {
		nvrhi::ShaderHandle VS;
		nvrhi::ShaderHandle PS;
		nvrhi::BindingLayoutHandle GlobalLayout;  
		nvrhi::BindingLayoutHandle MaterialLayout;

		bool operator==(const PipelineKey& other) const;
	};

	struct PipelineKeyHasher {
		std::size_t operator()(const PipelineKey& k) const;
	};

	class PipelineCache {
	public:
		void Init(nvrhi::IDevice* device);

		nvrhi::GraphicsPipelineHandle GetOrCreatePipeline(const PipelineKey& key, nvrhi::IFramebuffer* fb);

	private:
		nvrhi::DeviceHandle m_Device;
		std::unordered_map<PipelineKey, nvrhi::GraphicsPipelineHandle, PipelineKeyHasher> m_Cache;
	};
}
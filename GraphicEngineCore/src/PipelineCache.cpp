#include "PipelineCache.h"



bool Engine::PipelineKey::operator==(const PipelineKey& other) const {
	return VS == other.VS &&
		PS == other.PS &&
		GlobalLayout == other.GlobalLayout &&
		MaterialLayout == other.MaterialLayout;
}

std::size_t Engine::PipelineKeyHasher::operator()(const PipelineKey& k) const {
	std::size_t res = 0;
	auto hash_combine = [&res](void* ptr) {
		res ^= std::hash<void*>()(ptr) + 0x9e3779b9 + (res << 6) + (res >> 2);
		};
	hash_combine(k.VS.Get());
	hash_combine(k.PS.Get());
	hash_combine(k.GlobalLayout.Get());
	hash_combine(k.MaterialLayout.Get());
	return res;
}

void Engine::PipelineCache::Init(nvrhi::IDevice* device) {
	m_Device = device;
}

nvrhi::GraphicsPipelineHandle Engine::PipelineCache::GetOrCreatePipeline(const PipelineKey& key, nvrhi::IFramebuffer* fb) {
	auto it = m_Cache.find(key);
	if(it != m_Cache.end()) {
		return it->second;
	}


	auto vertexDesc = Renderer::Mesh::GetVertexAttributes();
	auto inputLayout = m_Device->createInputLayout(vertexDesc.data(), (uint32_t)vertexDesc.size(), key.VS);

	nvrhi::GraphicsPipelineDesc pdesc;
	pdesc.VS = key.VS;
	pdesc.PS = key.PS;
	pdesc.inputLayout = inputLayout;
	pdesc.bindingLayouts = { key.GlobalLayout, key.MaterialLayout };

	pdesc.renderState.depthStencilState.depthTestEnable = true;
	pdesc.renderState.depthStencilState.depthWriteEnable = true;
	pdesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;

	// pdesc.renderState.rasterState.fillMode = nvrhi::RasterFillMode::Solid;
	pdesc.renderState.rasterState.fillMode = nvrhi::RasterFillMode::Wireframe;
	pdesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;

	auto pipeline = m_Device->createGraphicsPipeline(pdesc, fb);

	m_Cache[key] = pipeline;

	return pipeline;
}

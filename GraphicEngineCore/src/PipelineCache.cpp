#include "PipelineCache.h"

#include <cstdint>

bool Engine::PipelineKey::operator==(const PipelineKey& other) const {
	return VS == other.VS &&
		PS == other.PS &&
		GlobalLayout == other.GlobalLayout &&
     MaterialLayout == other.MaterialLayout &&
		Wireframe == other.Wireframe &&
		DepthTest == other.DepthTest &&
		DepthWrite == other.DepthWrite &&
		CullMode == other.CullMode;
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
 hash_combine(reinterpret_cast<void*>(static_cast<uintptr_t>(k.Wireframe)));
	hash_combine(reinterpret_cast<void*>(static_cast<uintptr_t>(k.DepthTest)));
	hash_combine(reinterpret_cast<void*>(static_cast<uintptr_t>(k.DepthWrite)));
	hash_combine(reinterpret_cast<void*>(static_cast<uintptr_t>(k.CullMode)));
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
    pdesc.bindingLayouts = { key.GlobalLayout };
	if(key.MaterialLayout) {
		pdesc.bindingLayouts.push_back(key.MaterialLayout);
	}

 pdesc.renderState.depthStencilState.depthTestEnable = key.DepthTest;
	pdesc.renderState.depthStencilState.depthWriteEnable = key.DepthWrite;
	pdesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;

   pdesc.renderState.rasterState.fillMode = key.Wireframe ? nvrhi::RasterFillMode::Wireframe : nvrhi::RasterFillMode::Solid;
	pdesc.renderState.rasterState.cullMode = key.CullMode;
 pdesc.renderState.rasterState.frontCounterClockwise = false;

	auto pipeline = m_Device->createGraphicsPipeline(pdesc, fb);

	m_Cache[key] = pipeline;

	return pipeline;
}

#include "renderer/render_graph/RGBuilder.h"

namespace Renderer {
	RGBuilder::RGBuilder(RGResourceRegistry& registry, RGPassDependencies& passDeps)
		: m_Registry(registry), m_CurrentPassDeps(passDeps) {
	}

	RGResourceID RGBuilder::CreateTexture(const RGTextureDesc& desc) {
		return m_Registry.CreateResource(desc);
	}

	void RGBuilder::Read(RGResourceID id) {
		if (id.isValid()) {
			m_CurrentPassDeps.inputs.push_back(id);
		}
	}

	void RGBuilder::Write(RGResourceID id) {
		if (id.isValid()) {
			m_CurrentPassDeps.outputs.push_back(id);
		}
	}

	RGResourceID RGBuilder::CreateAndWriteTexture(const RGTextureDesc& desc) {
		RGResourceID newId = CreateTexture(desc);
		Write(newId);
		return newId;
	}

}
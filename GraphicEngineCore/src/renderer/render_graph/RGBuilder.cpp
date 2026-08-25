#include "renderer/render_graph/RGBuilder.h"

#include <algorithm>
#include <utility>

namespace Renderer {
	RGBuilder::RGBuilder(RGResourceRegistry& registry, RGPassDependencies& passDeps)
		: m_Registry(registry), m_CurrentPassDeps(passDeps) {
	}

	RGResourceID RGBuilder::CreateTexture(const RGTextureDesc& desc) {
		return m_Registry.CreateResource(desc);
	}

	RGResourceID RGBuilder::ImportTexture(const RGTextureDesc& desc, nvrhi::TextureHandle texture) {
		return m_Registry.ImportResource(desc, std::move(texture));
	}

	void RGBuilder::Read(RGResourceID id) {
		if (!m_Registry.IsValid(id)) {
			m_CurrentPassDeps.valid = false;
			m_CurrentPassDeps.error = "Read references an invalid resource ID";
			return;
		}
		if (std::find(m_CurrentPassDeps.inputs.begin(), m_CurrentPassDeps.inputs.end(), id) == m_CurrentPassDeps.inputs.end()) {
			m_CurrentPassDeps.inputs.push_back(id);
		}
	}

	void RGBuilder::Write(RGResourceID id) {
		if (!m_Registry.IsValid(id)) {
			m_CurrentPassDeps.valid = false;
			m_CurrentPassDeps.error = "Write references an invalid resource ID";
			return;
		}
		if (std::find(m_CurrentPassDeps.outputs.begin(), m_CurrentPassDeps.outputs.end(), id) != m_CurrentPassDeps.outputs.end()) {
			m_CurrentPassDeps.valid = false;
			m_CurrentPassDeps.error = "Resource is written more than once by one pass";
			return;
		}
		m_CurrentPassDeps.outputs.push_back(id);
	}

	void RGBuilder::ReadWrite(RGResourceID id) {
		Read(id);
		Write(id);
	}

	RGResourceID RGBuilder::CreateAndWriteTexture(const RGTextureDesc& desc) {
		RGResourceID newId = CreateTexture(desc);
		Write(newId);
		return newId;
	}

}

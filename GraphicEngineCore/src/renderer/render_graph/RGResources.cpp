#include "renderer/render_graph/RGResources.h"

namespace Renderer {
	RGResourceID RGResourceRegistry::CreateResource(const RGTextureDesc& desc) {
		RGResourceID id;
		id.value = static_cast<uint32_t>(m_Registry.size());
		RGResource resource;
		resource.desc = desc;
		m_Registry.push_back(resource);
		return id;
	}
}

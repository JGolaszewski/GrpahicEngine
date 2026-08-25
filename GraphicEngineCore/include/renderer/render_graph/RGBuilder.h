#pragma once

#include "CoreExport.h"

#include "renderer/render_graph/RGResources.h"

#include <string>
#include <vector>

namespace Renderer {
	struct RGPassDependencies {
		std::vector<RGResourceID> inputs;
		std::vector<RGResourceID> outputs;
		bool valid = true;
		std::string error;
	};

	class ENGINE_API RGBuilder {
	private:
		RGResourceRegistry& m_Registry;
		RGPassDependencies& m_CurrentPassDeps;

	public:
		RGBuilder(RGResourceRegistry& registry, RGPassDependencies& passDeps);

		RGResourceID CreateTexture(const RGTextureDesc& desc);
		RGResourceID ImportTexture(const RGTextureDesc& desc, nvrhi::TextureHandle texture);
		RGResourceID CreateAndWriteTexture(const RGTextureDesc& desc);

		void Read(RGResourceID id);
		void Write(RGResourceID id);
		void ReadWrite(RGResourceID id);
	};
}

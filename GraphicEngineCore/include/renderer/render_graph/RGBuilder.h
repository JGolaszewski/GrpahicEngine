#pragma once

#include "CoreExport.h"

namespace Renderer {
	struct RGPassDependencies {
		std::vector<RGResourceID> inputs;
		std::vector<RGResourceID> outputs;
	};

	class ENGINE_API RGBuilder {
	private:
		RGResourceRegistry& m_Registry;
		RGPassDependencies& m_CurrentPassDeps;

	public:
		RGBuilder(RGResourceRegistry& registry, RGPassDependencies& passDeps);

		RGResourceID CreateTexture(const RGTextureDesc& desc);
		RGResourceID CreateAndWriteTexture(const RGTextureDesc& desc);

		void Read(RGResourceID id);
		void Write(RGResourceID id);
	};
}
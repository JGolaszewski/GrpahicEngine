#pragma once 

#include <unordered_map>
#include <span>
#include "CoreExport.h"

namespace Renderer {


	struct RGResourceID {
		uint32_t value = UINT32_MAX;

		inline bool isValid() { return value != UINT32_MAX; }
	};



	struct RGTextureDesc {
		uint32_t width;
		uint32_t height;

		// TODO Add nvrhi type and Texture type
	};

	struct RGTextureDescHasher {
		std::size_t operator()(const RGTextureDesc& desc) const;
	};

	struct RGTextureDescEqual {
		bool operator()(const RGTextureDesc& a, const RGTextureDesc& b) const;
	};

		struct RGResource {
			RGTextureDesc desc;
			bool isImported = false;

			uint32_t firstPassUse = UINT32_MAX;
			uint32_t lastPassUse = 0;

			nvrhi::TextureHandle physicalHandle = nullptr;
		};

	class ENGINE_API RGResourceRegistry {
	public:
		RGResourceID CreateResource(const RGTextureDesc& desc);
		inline RGResource& GetResource(RGResourceID id) { return m_Registry[id.value]; }

		inline std::span<RGResource> GetAllResources() { return m_Registry; }
		inline std::span<const RGResource> GetAllResources() const { return m_Registry; }
	private:
		std::vector<RGResource> m_Registry;
	};
}
#pragma once 

#include "CoreExport.h"
#include "RGResources.h"

#include <nvrhi/nvrhi.h>

#include <unordered_map>

namespace Renderer {

	class ENGINE_API RGResourcePool {
	public:
		RGResourcePool(nvrhi::DeviceHandle device);

		nvrhi::TextureHandle Acquire(const RGTextureDesc& desc);
		void Release(const RGTextureDesc& desc, nvrhi::TextureHandle texture);
		void Clear();
	private:
		nvrhi::DeviceHandle m_Device;

		std::unordered_map<
			RGTextureDesc,
			std::vector<nvrhi::TextureHandle>,
			RGTextureDescHasher,
			RGTextureDescEqual	
		> m_FreeTextures;
	};


}
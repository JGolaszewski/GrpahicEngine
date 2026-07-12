#include "RGResourcePool.h"

namespace Renderer {
	RGResourcePool::RGResourcePool(nvrhi::DeviceHandle device) : m_Device(device) {}

	void RGResourcePool::Release(const RGTextureDesc& desc, nvrhi::TextureHandle texture)
	{
		m_FreeTextures[desc].push_back(texture);
	}

	void RGResourcePool::Clear()
	{
		m_FreeTextures.clear();
	}

	nvrhi::TextureHandle RGResourcePool::Acquire(const RGTextureDesc& desc) {
		auto& freeList = m_FreeTextures[desc];

		// Return a free texture if available, otherwise create a new one
		if (!freeList.empty()) {
			nvrhi::TextureHandle tex = freeList.back();
			freeList.pop_back();
			return tex;
		}

		nvrhi::TextureDesc nvrhiDesc;
		nvrhiDesc.width = desc.width;
		nvrhiDesc.height = desc.height;
		nvrhiDesc.isRenderTarget = true;

		// TODO: Set other properties of nvrhiDesc as needed
		return m_Device->createTexture(nvrhiDesc);
	}
}
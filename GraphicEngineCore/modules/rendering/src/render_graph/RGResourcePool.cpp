#include <utility>

#include "rendering/src/render_graph/RGResourcePool.h"

namespace Engine::Renderer
{
RGResourcePool::RGResourcePool(nvrhi::DeviceHandle device) : m_Device(std::move(device))
{
}

void RGResourcePool::Release(const RGTextureDesc &desc, const nvrhi::TextureHandle &texture)
{
    m_FreeTextures[desc].push_back(texture);
}

void RGResourcePool::Clear()
{
    m_FreeTextures.clear();
}

nvrhi::TextureHandle RGResourcePool::Acquire(const RGTextureDesc &desc)
{
    auto &freeList = m_FreeTextures[desc];

    if (!freeList.empty())
    {
        nvrhi::TextureHandle tex = freeList.back();
        freeList.pop_back();
        return tex;
    }

    nvrhi::TextureDesc nvrhiDesc;
    nvrhiDesc.width = desc.width;
    nvrhiDesc.height = desc.height;
    nvrhiDesc.arraySize = desc.arraySize;
    nvrhiDesc.mipLevels = desc.mipLevels;
    nvrhiDesc.format = desc.format;
    nvrhiDesc.dimension = desc.dimension;
    nvrhiDesc.isRenderTarget = desc.isRenderTarget;
    nvrhiDesc.isShaderResource = desc.isShaderResource;
    nvrhiDesc.isUAV = desc.isUnorderedAccess;

    if (desc.isShaderResource && (desc.format == nvrhi::Format::D32 || desc.format == nvrhi::Format::D24S8))
    {
        nvrhiDesc.isTypeless = true;
    }

    nvrhiDesc.initialState = nvrhi::ResourceStates::Common;

    nvrhiDesc.keepInitialState = true;

    return m_Device->createTexture(nvrhiDesc);
}
} // namespace Engine::Renderer

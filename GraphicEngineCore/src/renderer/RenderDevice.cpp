#include "renderer/RenderDevice.h"

#include <limits>

namespace Renderer
{
std::shared_ptr<Mesh> RenderDevice::CreateMesh(const std::vector<Vertex> &vertices,
                                               const std::vector<uint32_t> &indices, std::string &error,
                                               nvrhi::PrimitiveType primitiveType) const
{
    error.clear();
    if (!m_Device || vertices.empty() || vertices.size() > std::numeric_limits<uint32_t>::max() ||
        indices.size() > std::numeric_limits<uint32_t>::max())
    {
        error = "mesh creation requires a valid device, non-empty vertices, and counts that fit the renderer "
                "limits";
        return nullptr;
    }
    for (const uint32_t index : indices)
    {
        if (index >= vertices.size())
        {
            error = "mesh index references a vertex outside the supplied vertex data";
            return nullptr;
        }
    }
    auto mesh = std::make_shared<Mesh>(m_Device, vertices, indices, primitiveType);
    if (!mesh->IsValid())
    {
        error = "NVRHI failed to create mesh buffers";
        return nullptr;
    }
    return mesh;
}

std::shared_ptr<TextureResource> RenderDevice::CreateTexture(const Engine::ImportedImage &image, bool srgb,
                                                             std::string &error) const
{
    error.clear();
    if (!m_Device || image.width == 0 || image.height == 0 ||
        static_cast<size_t>(image.width) > std::numeric_limits<size_t>::max() / image.height / 4 ||
        image.rgba8.size() != static_cast<size_t>(image.width) * image.height * 4)
    {
        error = "texture creation requires valid RGBA8 pixels and a renderer device";
        return nullptr;
    }
    auto resource = std::make_shared<TextureResource>();
    nvrhi::TextureDesc textureDesc;
    textureDesc.setDimension(nvrhi::TextureDimension::Texture2D)
        .setWidth(image.width)
        .setHeight(image.height)
        .setDepth(1)
        .setArraySize(1)
        .setMipLevels(1)
        .setFormat(srgb ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM)
        .setDebugName(image.name.empty() ? "AssetTexture" : image.name)
        .enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    resource->Texture = m_Device->createTexture(textureDesc);
    if (!resource->Texture)
    {
        error = "NVRHI failed to create a texture resource";
        return nullptr;
    }

    nvrhi::SamplerDesc samplerDesc;
    samplerDesc.setAddressU(nvrhi::SamplerAddressMode::Wrap)
        .setAddressV(nvrhi::SamplerAddressMode::Wrap)
        .setAddressW(nvrhi::SamplerAddressMode::Wrap)
        .setMinFilter(true)
        .setMagFilter(true)
        .setMipFilter(false);
    resource->Sampler = m_Device->createSampler(samplerDesc);
    if (!resource->Sampler)
    {
        error = "NVRHI failed to create a texture sampler";
        return nullptr;
    }

    return resource;
}
} // namespace Renderer

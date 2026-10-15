#include "TextureBackend.h"

#include <utility>

namespace Engine::Renderer
{
struct TextureResource::Impl
{
    nvrhi::TextureHandle Texture;
    nvrhi::SamplerHandle Sampler;
};

TextureResource::TextureResource() : m_Impl(std::make_unique<Impl>())
{
}

TextureResource::~TextureResource() = default;

nvrhi::TextureHandle TextureBackendAccess::GetTexture(const TextureResource &resource)
{
    return resource.m_Impl->Texture;
}

void TextureBackendAccess::SetTexture(TextureResource &resource, nvrhi::TextureHandle texture)
{
    resource.m_Impl->Texture = std::move(texture);
}

nvrhi::SamplerHandle TextureBackendAccess::GetSampler(const TextureResource &resource)
{
    return resource.m_Impl->Sampler;
}

void TextureBackendAccess::SetSampler(TextureResource &resource, nvrhi::SamplerHandle sampler)
{
    resource.m_Impl->Sampler = std::move(sampler);
}
} // namespace Engine::Renderer

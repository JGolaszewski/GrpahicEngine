#include "TextureBackend.h"

#include <unordered_map>
#include <utility>

namespace Engine::Renderer {
struct TextureResource::Impl {
  nvrhi::TextureHandle Texture;
  nvrhi::SamplerHandle Sampler;
  nvrhi::SamplerDesc SamplerDescription;
  std::unordered_map<uint32_t, nvrhi::SamplerHandle> AnisotropicSamplers;
};

TextureResource::TextureResource() : m_Impl(std::make_unique<Impl>()) {}

TextureResource::~TextureResource() = default;

nvrhi::TextureHandle TextureBackendAccess::GetTexture(const TextureResource& resource) {
  return resource.m_Impl->Texture;
}

void TextureBackendAccess::SetTexture(TextureResource& resource, nvrhi::TextureHandle texture) {
  resource.m_Impl->Texture = std::move(texture);
}

nvrhi::SamplerHandle TextureBackendAccess::GetSampler(const TextureResource& resource) {
  return resource.m_Impl->Sampler;
}

void TextureBackendAccess::SetSampler(TextureResource& resource, nvrhi::SamplerHandle sampler) {
  resource.m_Impl->Sampler = std::move(sampler);
}

void TextureBackendAccess::SetSamplerDesc(TextureResource& resource, const nvrhi::SamplerDesc& desc) {
  resource.m_Impl->SamplerDescription = desc;
}

nvrhi::SamplerHandle TextureBackendAccess::GetSampler(const TextureResource& resource, uint32_t maxAnisotropy,
                                                      nvrhi::IDevice* device) {
  if (maxAnisotropy <= 1 || device == nullptr) {
    return GetSampler(resource);
  }
  if (const auto found = resource.m_Impl->AnisotropicSamplers.find(maxAnisotropy);
      found != resource.m_Impl->AnisotropicSamplers.end()) {
    return found->second;
  }
  nvrhi::SamplerDesc desc = resource.m_Impl->SamplerDescription;
  desc.setAllFilters(true).setMaxAnisotropy(static_cast<float>(maxAnisotropy));
  auto sampler = device->createSampler(desc);
  if (sampler != nullptr) {
    resource.m_Impl->AnisotropicSamplers.emplace(maxAnisotropy, sampler);
  }
  return sampler;
}
} // namespace Engine::Renderer

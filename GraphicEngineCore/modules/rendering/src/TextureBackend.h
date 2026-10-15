#pragma once

#include "rendering/include/renderer/TextureResource.h"

#include <nvrhi/nvrhi.h>

namespace Engine::Renderer
{
class TextureBackendAccess
{
  public:
    static nvrhi::TextureHandle GetTexture(const TextureResource &resource);
    static void SetTexture(TextureResource &resource, nvrhi::TextureHandle texture);
    static nvrhi::SamplerHandle GetSampler(const TextureResource &resource);
    static void SetSampler(TextureResource &resource, nvrhi::SamplerHandle sampler);
};
} // namespace Engine::Renderer

#pragma once

#include "rendering/include/renderer/Material.h"

#include <nvrhi/nvrhi.h>

namespace Engine::Renderer {
class ENGINE_API MaterialBackendAccess {
public:
  static nvrhi::BufferHandle GetConstantBuffer(const Material& material);
  static void SetConstantBuffer(Material& material, nvrhi::BufferHandle buffer);
  static nvrhi::BindingSetHandle GetBindingSet(const Material& material);
  static void SetBindingSet(Material& material, nvrhi::BindingSetHandle bindingSet);
  static nvrhi::TextureHandle GetNdfResidualTexture(const Material& material);
  static void SetNdfResidualTexture(Material& material, nvrhi::TextureHandle texture);
  static nvrhi::TextureHandle GetNdfDistributionTexture(const Material& material);
  static void SetNdfDistributionTexture(Material& material, nvrhi::TextureHandle texture);
  static std::shared_ptr<const PrismNdfData> GetUploadedNdf(const Material& material);
  static void SetUploadedNdf(Material& material, std::shared_ptr<const PrismNdfData> payload);
};
} // namespace Engine::Renderer

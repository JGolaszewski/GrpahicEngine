#include "rendering/src/MaterialSystem.h"
#include "MaterialBackend.h"
#include "TextureBackend.h"
#include "rendering/include/renderer/PrismNdfMath.h"

#include <algorithm>
#include <array>
#include <span>

namespace Engine::Renderer {
namespace {
auto CreateNdfResidualTexture(nvrhi::IDevice* device) -> nvrhi::TextureHandle {
  if (device == nullptr) {
    return nullptr;
  }
  nvrhi::TextureDesc desc;
  desc.setDimension(nvrhi::TextureDimension::Texture2D)
      .setWidth(PrismNdfGridSize)
      .setHeight(PrismNdfGridSize)
      .setArraySize(1)
      .setMipLevels(1)
      .setFormat(nvrhi::Format::R32_FLOAT)
      .setDebugName("Material_PrismNdfResidual")
      .enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
  return device->createTexture(desc);
}

auto BuildNdfDistribution(const PrismNdfData& payload) -> std::array<float, PrismNdfSampleCount> {
  std::array<float, PrismNdfSampleCount> distribution{};
  if (payload.residuals.size() != PrismNdfSampleCount) {
    return distribution;
  }
  for (uint32_t row = 0; row < PrismNdfGridSize; ++row) {
    const double y = 2.0 * row / static_cast<double>(PrismNdfGridSize - 1) - 1.0;
    for (uint32_t column = 0; column < PrismNdfGridSize; ++column) {
      const double x = 2.0 * column / static_cast<double>(PrismNdfGridSize - 1) - 1.0;
      if (x * x + y * y > 1.0) {
        continue;
      }
      double base = 0.0;
      for (uint32_t coefficient = 0; coefficient < PrismNdfCoefficientCount; ++coefficient) {
        base += payload.zernikeCoefficients[coefficient] * EvaluatePrismZernikeBasis(coefficient, x, y);
      }
      const size_t index = static_cast<size_t>(row) * PrismNdfGridSize + column;
      const double residual = payload.residualsEnabled ? payload.residuals[index] : 0.0;
      distribution[index] = static_cast<float>((std::max)(base + residual, 0.0) * payload.normalization);
    }
  }
  return distribution;
}

void UploadNdfResidual(nvrhi::ICommandList* commandList, const nvrhi::TextureHandle& texture,
                       std::span<const float> residuals) {
  if (commandList != nullptr && texture != nullptr && residuals.size() == PrismNdfSampleCount) {
    commandList->writeTexture(texture, 0, 0, residuals.data(), PrismNdfGridSize * sizeof(float), 0);
  }
}

auto MakeMaterialBindingSet(nvrhi::IDevice* device, const nvrhi::BindingLayoutHandle& layout,
                            const nvrhi::BufferHandle& constants,
                            const MaterialProperties& properties,
                            const std::array<std::shared_ptr<TextureResource>, 5>& textures,
                            const nvrhi::TextureHandle& ndfResidual, const nvrhi::TextureHandle& ndfDistribution,
                            const nvrhi::SamplerHandle& ndfSampler, uint32_t maxAnisotropy) -> nvrhi::BindingSetHandle {
  if (device == nullptr || layout == nullptr || constants == nullptr || ndfResidual == nullptr ||
      ndfDistribution == nullptr || ndfSampler == nullptr ||
      std::ranges::any_of(textures, [](const auto& texture) { return texture == nullptr; })) {
    return nullptr;
  }
  const auto sampler = [device, &properties, maxAnisotropy](const auto& texture, size_t slot) {
    const uint32_t level = ResolveTextureAnisotropy(properties.TextureAnisotropyLevels[slot], maxAnisotropy);
    return TextureBackendAccess::GetSampler(*texture, level, device);
  };
  nvrhi::BindingSetDesc setDesc;
  setDesc.bindings = {
      nvrhi::BindingSetItem::ConstantBuffer(1, constants),
      nvrhi::BindingSetItem::Texture_SRV(0, TextureBackendAccess::GetTexture(*textures[0])),
      nvrhi::BindingSetItem::Sampler(0, sampler(textures[0], 0)),
      nvrhi::BindingSetItem::Texture_SRV(1, TextureBackendAccess::GetTexture(*textures[1])),
      nvrhi::BindingSetItem::Sampler(1, sampler(textures[1], 1)),
      nvrhi::BindingSetItem::Texture_SRV(2, TextureBackendAccess::GetTexture(*textures[2])),
      nvrhi::BindingSetItem::Sampler(2, sampler(textures[2], 2)),
      nvrhi::BindingSetItem::Texture_SRV(3, ndfResidual),
      nvrhi::BindingSetItem::Sampler(3, ndfSampler),
      nvrhi::BindingSetItem::Texture_SRV(6, ndfDistribution),
      nvrhi::BindingSetItem::Sampler(6, ndfSampler),
      nvrhi::BindingSetItem::Texture_SRV(4, TextureBackendAccess::GetTexture(*textures[3])),
      nvrhi::BindingSetItem::Sampler(4, sampler(textures[3], 3)),
      nvrhi::BindingSetItem::Texture_SRV(5, TextureBackendAccess::GetTexture(*textures[4])),
      nvrhi::BindingSetItem::Sampler(5, sampler(textures[4], 4)),
  };
  if (std::ranges::any_of(setDesc.bindings, [](const auto& binding) { return binding.resourceHandle == nullptr; })) {
    return nullptr;
  }
  return device->createBindingSet(setDesc, layout);
}
} // namespace

MaterialSystem::MaterialSystem(nvrhi::IDevice* device, nvrhi::ShaderHandle standardVS, nvrhi::ShaderHandle standardPS,
                               std::shared_ptr<TextureResource> fallbackColor,
                               std::shared_ptr<TextureResource> fallbackNormal)
    : m_Device(device), m_StandardVS(std::move(standardVS)), m_StandardPS(std::move(standardPS)),
      m_FallbackColor(std::move(fallbackColor)), m_FallbackNormal(std::move(fallbackNormal)) {
  nvrhi::BindingLayoutDesc layoutDesc;
  layoutDesc.visibility = nvrhi::ShaderType::All;
  layoutDesc.bindings = {
      nvrhi::BindingLayoutItem::ConstantBuffer(1), nvrhi::BindingLayoutItem::Texture_SRV(0),
      nvrhi::BindingLayoutItem::Sampler(0),        nvrhi::BindingLayoutItem::Texture_SRV(1),
      nvrhi::BindingLayoutItem::Sampler(1),
  };
  layoutDesc.bindings.insert(layoutDesc.bindings.end(), {
                                                            nvrhi::BindingLayoutItem::Texture_SRV(3),
                                                            nvrhi::BindingLayoutItem::Sampler(3),
                                                            nvrhi::BindingLayoutItem::Texture_SRV(6),
                                                            nvrhi::BindingLayoutItem::Sampler(6),
                                                            nvrhi::BindingLayoutItem::Texture_SRV(2),
                                                            nvrhi::BindingLayoutItem::Sampler(2),
                                                            nvrhi::BindingLayoutItem::Texture_SRV(4),
                                                            nvrhi::BindingLayoutItem::Sampler(4),
                                                            nvrhi::BindingLayoutItem::Texture_SRV(5),
                                                            nvrhi::BindingLayoutItem::Sampler(5),
                                                        });
  m_StandardLayout = m_Device->createBindingLayout(layoutDesc);
  m_FallbackNdfResidual = CreateNdfResidualTexture(m_Device);
  m_FallbackNdfDistribution = CreateNdfResidualTexture(m_Device);
  nvrhi::SamplerDesc residualSamplerDesc;
  residualSamplerDesc.setAddressU(nvrhi::SamplerAddressMode::Clamp)
      .setAddressV(nvrhi::SamplerAddressMode::Clamp)
      .setAddressW(nvrhi::SamplerAddressMode::Clamp)
      .setMinFilter(true)
      .setMagFilter(true)
      .setMipFilter(false);
  m_NdfResidualSampler = m_Device->createSampler(residualSamplerDesc);
  if (m_FallbackNdfResidual != nullptr && m_FallbackNdfDistribution != nullptr && m_NdfResidualSampler != nullptr) {
    nvrhi::CommandListHandle commandList = m_Device->createCommandList();
    if (commandList != nullptr) {
      const std::array<float, PrismNdfSampleCount> zeroResidual{};
      commandList->open();
      UploadNdfResidual(commandList, m_FallbackNdfResidual, zeroResidual);
      UploadNdfResidual(commandList, m_FallbackNdfDistribution, zeroResidual);
      commandList->close();
      m_Device->executeCommandList(commandList);
      m_Device->waitForIdle();
    }
  }
}

MaterialSystem::~MaterialSystem() { m_ActiveMaterials.clear(); }

auto MaterialSystem::RegisterMaterial(std::shared_ptr<Material> material) -> bool {
  if (!material || std::ranges::any_of(m_ActiveMaterials,
                                       [&material](const auto& weak) -> auto { return weak.lock() == material; })) {
    return false;
  }
  if ((m_Device == nullptr) || (m_StandardLayout == nullptr) || !m_FallbackColor || !m_FallbackNormal) {
    return false;
  }

  // Allocate constant buffer for the material
  nvrhi::BufferDesc bufferDesc;
  bufferDesc.byteSize = sizeof(MaterialGpu);
  bufferDesc.isConstantBuffer = true;
  bufferDesc.isVolatile = false;
  bufferDesc.debugName = "Material_ConstantBuffer";
  auto constantBuffer = m_Device->createBuffer(bufferDesc);
  MaterialBackendAccess::SetConstantBuffer(*material, constantBuffer);
  if (constantBuffer == nullptr) {
    return false;
  }

  material->m_BaseColorTexture = m_FallbackColor;
  material->m_NormalTexture = m_FallbackNormal;
  material->m_MetallicRoughnessTexture = m_FallbackColor;
  material->m_OcclusionTexture = m_FallbackColor;
  material->m_EmissiveTexture = m_FallbackColor;
  if (material->GetProperties().GeneratedNdf) {
    MaterialBackendAccess::SetNdfResidualTexture(*material, CreateNdfResidualTexture(m_Device));
    MaterialBackendAccess::SetNdfDistributionTexture(*material, CreateNdfResidualTexture(m_Device));
  }
  material->m_DefaultBaseColorTexture = m_FallbackColor;
  material->m_DefaultNormalTexture = m_FallbackNormal;
  material->m_HasTextureOverrides = false;
  auto bindingSet = MakeMaterialBindingSet(m_Device, m_StandardLayout, constantBuffer, material->GetProperties(),
                                           {material->m_BaseColorTexture, material->m_NormalTexture,
                                            material->m_MetallicRoughnessTexture, material->m_OcclusionTexture,
                                            material->m_EmissiveTexture},
                                           MaterialBackendAccess::GetNdfResidualTexture(*material) != nullptr
                                               ? MaterialBackendAccess::GetNdfResidualTexture(*material)
                                               : m_FallbackNdfResidual,
                                           MaterialBackendAccess::GetNdfDistributionTexture(*material) != nullptr
                                               ? MaterialBackendAccess::GetNdfDistributionTexture(*material)
                                               : m_FallbackNdfDistribution,
                                           m_NdfResidualSampler, m_MaxTextureAnisotropy);
  MaterialBackendAccess::SetBindingSet(*material, bindingSet);
  if (bindingSet == nullptr) {
    MaterialBackendAccess::SetConstantBuffer(*material, nullptr);
    return false;
  }
  material->m_IsDirty = true;

  m_ActiveMaterials.push_back(material);
  return true;
}

void MaterialSystem::UnregisterMaterial(std::shared_ptr<Material> material) {
  std::erase_if(m_ActiveMaterials, [&material](const auto& weak) -> auto {
    const auto registered = weak.lock();
    return !registered || registered == material;
  });
}

void MaterialSystem::UpdateDirtyMaterials(nvrhi::ICommandList* commandList) {
  if (commandList == nullptr) {
    return;
  }

  for (auto it = m_ActiveMaterials.begin(); it != m_ActiveMaterials.end();) {
    if (const auto material = it->lock()) {
      const auto constantBuffer = MaterialBackendAccess::GetConstantBuffer(*material);
      if (material->IsDirty() && (constantBuffer != nullptr)) {
        const auto properties = material->GetProperties();
        bool residualReady = true;
        if (properties.GeneratedNdf != MaterialBackendAccess::GetUploadedNdf(*material)) {
          if (properties.GeneratedNdf) {
            auto residualTexture = MaterialBackendAccess::GetNdfResidualTexture(*material);
            if (residualTexture == nullptr) {
              residualTexture = CreateNdfResidualTexture(m_Device);
              MaterialBackendAccess::SetNdfResidualTexture(*material, residualTexture);
            }
            auto distributionTexture = MaterialBackendAccess::GetNdfDistributionTexture(*material);
            if (distributionTexture == nullptr) {
              distributionTexture = CreateNdfResidualTexture(m_Device);
              MaterialBackendAccess::SetNdfDistributionTexture(*material, distributionTexture);
            }
            if (residualTexture == nullptr || distributionTexture == nullptr) {
              residualReady = false;
            } else {
              UploadNdfResidual(commandList, residualTexture, properties.GeneratedNdf->residuals);
              UploadNdfResidual(commandList, distributionTexture, BuildNdfDistribution(*properties.GeneratedNdf));
              MaterialBackendAccess::SetUploadedNdf(*material, properties.GeneratedNdf);
            }
          } else {
            MaterialBackendAccess::SetNdfResidualTexture(*material, nullptr);
            MaterialBackendAccess::SetNdfDistributionTexture(*material, nullptr);
            MaterialBackendAccess::SetUploadedNdf(*material, {});
          }
        }
        if (!residualReady) {
          ++it;
          continue;
        }
        commandList->writeBuffer(constantBuffer, &material->m_Constants, sizeof(MaterialGpu));
        auto bindingSet = MakeMaterialBindingSet(m_Device, m_StandardLayout, constantBuffer, properties,
                                                 {material->m_BaseColorTexture, material->m_NormalTexture,
                                                  material->m_MetallicRoughnessTexture, material->m_OcclusionTexture,
                                                  material->m_EmissiveTexture},
                                                 MaterialBackendAccess::GetNdfResidualTexture(*material) != nullptr
                                                     ? MaterialBackendAccess::GetNdfResidualTexture(*material)
                                                     : m_FallbackNdfResidual,
                                                 MaterialBackendAccess::GetNdfDistributionTexture(*material) != nullptr
                                                     ? MaterialBackendAccess::GetNdfDistributionTexture(*material)
                                                     : m_FallbackNdfDistribution,
                                                 m_NdfResidualSampler, m_MaxTextureAnisotropy);
        if (bindingSet != nullptr) {
          MaterialBackendAccess::SetBindingSet(*material, std::move(bindingSet));
          material->ClearDirtyFlag();
        }
      }
      ++it;
    } else {
      it = m_ActiveMaterials.erase(it);
    }
  }
}

void MaterialSystem::SetMaxTextureAnisotropy(uint32_t maxAnisotropy) noexcept {
  if (!IsSupportedTextureAnisotropy(maxAnisotropy) || maxAnisotropy == m_MaxTextureAnisotropy) {
    return;
  }
  m_MaxTextureAnisotropy = maxAnisotropy;
  for (const auto& weak : m_ActiveMaterials) {
    if (const auto material = weak.lock()) {
      material->m_IsDirty = true;
    }
  }
}

auto MaterialSystem::GetFallbackColorTexture() const -> nvrhi::TextureHandle {
  return m_FallbackColor ? TextureBackendAccess::GetTexture(*m_FallbackColor) : nullptr;
}

auto MaterialSystem::CreateMaterial(const MaterialProperties& properties) -> std::shared_ptr<Material> {
  if (properties.GeneratedNdf && properties.GeneratedNdf->residuals.size() != PrismNdfSampleCount) {
    return nullptr;
  }
  auto material = std::make_shared<Material>(m_NextMaterialID++);
  material->SetProperties(properties);
  return RegisterMaterial(material) ? material : nullptr;
}

auto MaterialSystem::SetTextureBindings(const std::shared_ptr<Material>& material,
                                        std::shared_ptr<TextureResource> baseColor,
                                        std::shared_ptr<TextureResource> normal,
                                        std::optional<std::string> baseColorGuid, std::optional<std::string> normalGuid,
                                        bool setImportedDefaults) -> bool {
  if (!material) {
    return false;
  }
  if (!SetPbrTextureBindings(material, std::move(baseColor), std::move(normal), material->m_MetallicRoughnessTexture,
                             material->m_OcclusionTexture, material->m_EmissiveTexture, setImportedDefaults)) {
    return false;
  }
  material->m_BaseColorTextureGuid = std::move(baseColorGuid);
  material->m_NormalTextureGuid = std::move(normalGuid);
  return true;
}

auto MaterialSystem::SetPbrTextureBindings(const std::shared_ptr<Material>& material,
                                           std::shared_ptr<TextureResource> baseColor,
                                           std::shared_ptr<TextureResource> normal,
                                           std::shared_ptr<TextureResource> metallicRoughness,
                                           std::shared_ptr<TextureResource> occlusion,
                                           std::shared_ptr<TextureResource> emissive, bool setImportedDefaults)
    -> bool {
  if (!material || (MaterialBackendAccess::GetConstantBuffer(*material) == nullptr) || (m_Device == nullptr) ||
      (m_StandardLayout == nullptr)) {
    return false;
  }
  if (!baseColor) {
    baseColor = m_FallbackColor;
  }
  if (!normal) {
    normal = m_FallbackNormal;
  }
  if (!metallicRoughness) {
    metallicRoughness = m_FallbackColor;
  }
  if (!occlusion) {
    occlusion = m_FallbackColor;
  }
  if (!emissive) {
    emissive = m_FallbackColor;
  }
  if (!baseColor || !normal || !metallicRoughness || !occlusion || !emissive) {
    return false;
  }
  auto bindings =
      MakeMaterialBindingSet(m_Device, m_StandardLayout, MaterialBackendAccess::GetConstantBuffer(*material),
                             material->GetProperties(), {baseColor, normal, metallicRoughness, occlusion, emissive},
                             MaterialBackendAccess::GetNdfResidualTexture(*material) != nullptr
                                 ? MaterialBackendAccess::GetNdfResidualTexture(*material)
                                 : m_FallbackNdfResidual,
                             MaterialBackendAccess::GetNdfDistributionTexture(*material) != nullptr
                                 ? MaterialBackendAccess::GetNdfDistributionTexture(*material)
                                 : m_FallbackNdfDistribution,
                             m_NdfResidualSampler, m_MaxTextureAnisotropy);
  if (bindings == nullptr) {
    return false;
  }
  material->m_BaseColorTexture = std::move(baseColor);
  material->m_NormalTexture = std::move(normal);
  material->m_MetallicRoughnessTexture = std::move(metallicRoughness);
  material->m_OcclusionTexture = std::move(occlusion);
  material->m_EmissiveTexture = std::move(emissive);
  if (setImportedDefaults) {
    material->m_DefaultBaseColorTexture = material->m_BaseColorTexture;
    material->m_DefaultNormalTexture = material->m_NormalTexture;
  }
  material->m_HasTextureOverrides =
      material->m_BaseColorTexture != m_FallbackColor || material->m_NormalTexture != m_FallbackNormal ||
      material->m_MetallicRoughnessTexture != m_FallbackColor || material->m_OcclusionTexture != m_FallbackColor ||
      material->m_EmissiveTexture != m_FallbackColor;
  MaterialBackendAccess::SetBindingSet(*material, std::move(bindings));
  return true;
}

auto MaterialSystem::GetStandardMaterialLayout() const -> nvrhi::BindingLayoutHandle { return m_StandardLayout; }
} // namespace Engine::Renderer

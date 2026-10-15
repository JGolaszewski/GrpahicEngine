#include "rendering/src/MaterialSystem.h"
#include "MaterialBackend.h"
#include "TextureBackend.h"

#include <algorithm>

namespace Engine::Renderer {
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
                                                            nvrhi::BindingLayoutItem::Texture_SRV(2),
                                                            nvrhi::BindingLayoutItem::Sampler(2),
                                                            nvrhi::BindingLayoutItem::Texture_SRV(4),
                                                            nvrhi::BindingLayoutItem::Sampler(4),
                                                            nvrhi::BindingLayoutItem::Texture_SRV(5),
                                                            nvrhi::BindingLayoutItem::Sampler(5),
                                                        });
  m_StandardLayout = m_Device->createBindingLayout(layoutDesc);
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
  material->m_DefaultBaseColorTexture = m_FallbackColor;
  material->m_DefaultNormalTexture = m_FallbackNormal;
  material->m_HasTextureOverrides = false;
  nvrhi::BindingSetDesc setDesc;
  setDesc.bindings = {
      nvrhi::BindingSetItem::ConstantBuffer(1, constantBuffer),
      nvrhi::BindingSetItem::Texture_SRV(0, TextureBackendAccess::GetTexture(*material->m_BaseColorTexture)),
      nvrhi::BindingSetItem::Sampler(0, TextureBackendAccess::GetSampler(*material->m_BaseColorTexture)),
      nvrhi::BindingSetItem::Texture_SRV(1, TextureBackendAccess::GetTexture(*material->m_NormalTexture)),
      nvrhi::BindingSetItem::Sampler(1, TextureBackendAccess::GetSampler(*material->m_NormalTexture)),
      nvrhi::BindingSetItem::Texture_SRV(2, TextureBackendAccess::GetTexture(*material->m_MetallicRoughnessTexture)),
      nvrhi::BindingSetItem::Sampler(2, TextureBackendAccess::GetSampler(*material->m_MetallicRoughnessTexture)),
      nvrhi::BindingSetItem::Texture_SRV(4, TextureBackendAccess::GetTexture(*material->m_OcclusionTexture)),
      nvrhi::BindingSetItem::Sampler(4, TextureBackendAccess::GetSampler(*material->m_OcclusionTexture)),
      nvrhi::BindingSetItem::Texture_SRV(5, TextureBackendAccess::GetTexture(*material->m_EmissiveTexture)),
      nvrhi::BindingSetItem::Sampler(5, TextureBackendAccess::GetSampler(*material->m_EmissiveTexture)),
  };
  auto bindingSet = m_Device->createBindingSet(setDesc, m_StandardLayout);
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
        commandList->writeBuffer(constantBuffer, &material->m_Constants, sizeof(MaterialGpu));
        material->ClearDirtyFlag();
      }
      ++it;
    } else {
      it = m_ActiveMaterials.erase(it);
    }
  }
}

auto MaterialSystem::CreateMaterial(const MaterialProperties& properties) -> std::shared_ptr<Material> {
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
  nvrhi::BindingSetDesc setDesc;
  setDesc.bindings = {
      nvrhi::BindingSetItem::ConstantBuffer(1, MaterialBackendAccess::GetConstantBuffer(*material)),
      nvrhi::BindingSetItem::Texture_SRV(0, TextureBackendAccess::GetTexture(*baseColor)),
      nvrhi::BindingSetItem::Sampler(0, TextureBackendAccess::GetSampler(*baseColor)),
      nvrhi::BindingSetItem::Texture_SRV(1, TextureBackendAccess::GetTexture(*normal)),
      nvrhi::BindingSetItem::Sampler(1, TextureBackendAccess::GetSampler(*normal)),
      nvrhi::BindingSetItem::Texture_SRV(2, TextureBackendAccess::GetTexture(*metallicRoughness)),
      nvrhi::BindingSetItem::Sampler(2, TextureBackendAccess::GetSampler(*metallicRoughness)),
      nvrhi::BindingSetItem::Texture_SRV(4, TextureBackendAccess::GetTexture(*occlusion)),
      nvrhi::BindingSetItem::Sampler(4, TextureBackendAccess::GetSampler(*occlusion)),
      nvrhi::BindingSetItem::Texture_SRV(5, TextureBackendAccess::GetTexture(*emissive)),
      nvrhi::BindingSetItem::Sampler(5, TextureBackendAccess::GetSampler(*emissive)),
  };
  auto bindings = m_Device->createBindingSet(setDesc, m_StandardLayout);
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

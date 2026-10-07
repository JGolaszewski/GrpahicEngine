#include "rendering/include/renderer/MaterialSystem.h"

#include <algorithm>

namespace Engine::Renderer
{
MaterialSystem::MaterialSystem(nvrhi::IDevice *device, nvrhi::ShaderHandle standardVS,
                               nvrhi::ShaderHandle standardPS, std::shared_ptr<TextureResource> fallbackColor,
                               std::shared_ptr<TextureResource> fallbackNormal)
    : m_Device(device), m_StandardVS(standardVS), m_StandardPS(standardPS),
      m_FallbackColor(std::move(fallbackColor)), m_FallbackNormal(std::move(fallbackNormal))
{
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::All;
    layoutDesc.bindings = {nvrhi::BindingLayoutItem::ConstantBuffer(1),
                           nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Sampler(0),
                           nvrhi::BindingLayoutItem::Texture_SRV(1), nvrhi::BindingLayoutItem::Sampler(1)};
    layoutDesc.bindings.insert(layoutDesc.bindings.end(), {
        nvrhi::BindingLayoutItem::Texture_SRV(2), nvrhi::BindingLayoutItem::Sampler(2),
        nvrhi::BindingLayoutItem::Texture_SRV(4), nvrhi::BindingLayoutItem::Sampler(4),
        nvrhi::BindingLayoutItem::Texture_SRV(5), nvrhi::BindingLayoutItem::Sampler(5)});
    m_StandardLayout = m_Device->createBindingLayout(layoutDesc);
}

MaterialSystem::~MaterialSystem()
{
    m_ActiveMaterials.clear();
}

bool MaterialSystem::RegisterMaterial(std::shared_ptr<Material> material)
{
    if (!material || std::any_of(m_ActiveMaterials.begin(), m_ActiveMaterials.end(),
                                 [&material](const auto &weak) { return weak.lock() == material; }))
    {
        return false;
    }
    if (!m_Device || !m_StandardLayout || !m_FallbackColor || !m_FallbackNormal)
    {
        return false;
    }

    // Allocate constant buffer for the material
    nvrhi::BufferDesc bufferDesc;
    bufferDesc.byteSize = sizeof(MaterialGpu);
    bufferDesc.isConstantBuffer = true;
    bufferDesc.isVolatile = false;
    bufferDesc.debugName = "Material_ConstantBuffer";
    material->m_ConstantBuffer = m_Device->createBuffer(bufferDesc);
    if (!material->m_ConstantBuffer)
    {
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
    setDesc.bindings = {nvrhi::BindingSetItem::ConstantBuffer(1, material->m_ConstantBuffer),
                        nvrhi::BindingSetItem::Texture_SRV(0, material->m_BaseColorTexture->Texture),
                        nvrhi::BindingSetItem::Sampler(0, material->m_BaseColorTexture->Sampler),
                        nvrhi::BindingSetItem::Texture_SRV(1, material->m_NormalTexture->Texture),
                        nvrhi::BindingSetItem::Sampler(1, material->m_NormalTexture->Sampler),
                        nvrhi::BindingSetItem::Texture_SRV(2, material->m_MetallicRoughnessTexture->Texture),
                        nvrhi::BindingSetItem::Sampler(2, material->m_MetallicRoughnessTexture->Sampler),
                        nvrhi::BindingSetItem::Texture_SRV(4, material->m_OcclusionTexture->Texture),
                        nvrhi::BindingSetItem::Sampler(4, material->m_OcclusionTexture->Sampler),
                        nvrhi::BindingSetItem::Texture_SRV(5, material->m_EmissiveTexture->Texture),
                        nvrhi::BindingSetItem::Sampler(5, material->m_EmissiveTexture->Sampler)};
    material->m_BindingSet = m_Device->createBindingSet(setDesc, m_StandardLayout);
    if (!material->m_BindingSet)
    {
        material->m_ConstantBuffer = nullptr;
        return false;
    }
    material->m_IsDirty = true;

    m_ActiveMaterials.push_back(std::move(material));
    return true;
}

void MaterialSystem::UnregisterMaterial(std::shared_ptr<Material> material)
{
    std::erase_if(m_ActiveMaterials, [&material](const auto &weak) {
        const auto registered = weak.lock();
        return !registered || registered == material;
    });
}

void MaterialSystem::UpdateDirtyMaterials(nvrhi::ICommandList *commandList)
{
    if (!commandList)
    {
        return;
    }

    for (auto it = m_ActiveMaterials.begin(); it != m_ActiveMaterials.end();)
    {
        if (const auto material = it->lock())
        {
            if (material->IsDirty() && material->m_ConstantBuffer)
            {
                commandList->writeBuffer(material->m_ConstantBuffer, &material->m_Constants,
                                         sizeof(MaterialGpu));
                material->ClearDirtyFlag();
            }
            ++it;
        }
        else
        {
            it = m_ActiveMaterials.erase(it);
        }
    }
}

std::shared_ptr<Material> MaterialSystem::CreateMaterial(const MaterialProperties &properties)
{
    auto material = std::make_shared<Material>(m_NextMaterialID++);
    material->SetProperties(properties);
    return RegisterMaterial(material) ? material : nullptr;
}

bool MaterialSystem::SetTextureBindings(const std::shared_ptr<Material> &material,
                                        std::shared_ptr<TextureResource> baseColor,
                                        std::shared_ptr<TextureResource> normal,
                                        std::optional<std::string> baseColorGuid,
                                        std::optional<std::string> normalGuid, bool setImportedDefaults)
{
    if (!material)
        return false;
    if (!SetPbrTextureBindings(material, std::move(baseColor), std::move(normal),
                               material->m_MetallicRoughnessTexture, material->m_OcclusionTexture,
                               material->m_EmissiveTexture, setImportedDefaults))
        return false;
    material->m_BaseColorTextureGuid = std::move(baseColorGuid);
    material->m_NormalTextureGuid = std::move(normalGuid);
    return true;
}

bool MaterialSystem::SetPbrTextureBindings(const std::shared_ptr<Material> &material,
                                           std::shared_ptr<TextureResource> baseColor,
                                           std::shared_ptr<TextureResource> normal,
                                           std::shared_ptr<TextureResource> metallicRoughness,
                                           std::shared_ptr<TextureResource> occlusion,
                                           std::shared_ptr<TextureResource> emissive,
                                           bool setImportedDefaults)
{
    if (!material || !material->m_ConstantBuffer || !m_Device || !m_StandardLayout)
        return false;
    if (!baseColor) baseColor = m_FallbackColor;
    if (!normal) normal = m_FallbackNormal;
    if (!metallicRoughness) metallicRoughness = m_FallbackColor;
    if (!occlusion) occlusion = m_FallbackColor;
    if (!emissive) emissive = m_FallbackColor;
    if (!baseColor || !normal || !metallicRoughness || !occlusion || !emissive)
        return false;
    nvrhi::BindingSetDesc setDesc;
    setDesc.bindings = {nvrhi::BindingSetItem::ConstantBuffer(1, material->m_ConstantBuffer),
                        nvrhi::BindingSetItem::Texture_SRV(0, baseColor->Texture),
                        nvrhi::BindingSetItem::Sampler(0, baseColor->Sampler),
                        nvrhi::BindingSetItem::Texture_SRV(1, normal->Texture),
                        nvrhi::BindingSetItem::Sampler(1, normal->Sampler),
                        nvrhi::BindingSetItem::Texture_SRV(2, metallicRoughness->Texture),
                        nvrhi::BindingSetItem::Sampler(2, metallicRoughness->Sampler),
                        nvrhi::BindingSetItem::Texture_SRV(4, occlusion->Texture),
                        nvrhi::BindingSetItem::Sampler(4, occlusion->Sampler),
                        nvrhi::BindingSetItem::Texture_SRV(5, emissive->Texture),
                        nvrhi::BindingSetItem::Sampler(5, emissive->Sampler)};
    auto bindings = m_Device->createBindingSet(setDesc, m_StandardLayout);
    if (!bindings)
        return false;
    material->m_BaseColorTexture = std::move(baseColor);
    material->m_NormalTexture = std::move(normal);
    material->m_MetallicRoughnessTexture = std::move(metallicRoughness);
    material->m_OcclusionTexture = std::move(occlusion);
    material->m_EmissiveTexture = std::move(emissive);
    if (setImportedDefaults)
    {
        material->m_DefaultBaseColorTexture = material->m_BaseColorTexture;
        material->m_DefaultNormalTexture = material->m_NormalTexture;
    }
    material->m_HasTextureOverrides = material->m_BaseColorTexture != m_FallbackColor ||
                                      material->m_NormalTexture != m_FallbackNormal ||
                                      material->m_MetallicRoughnessTexture != m_FallbackColor ||
                                      material->m_OcclusionTexture != m_FallbackColor ||
                                      material->m_EmissiveTexture != m_FallbackColor;
    material->m_BindingSet = std::move(bindings);
    return true;
}

nvrhi::BindingLayoutHandle MaterialSystem::GetStandardMaterialLayout() const
{
    return m_StandardLayout;
}
} // namespace Engine::Renderer

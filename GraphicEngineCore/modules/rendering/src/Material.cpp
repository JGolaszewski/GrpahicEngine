#include "rendering/include/renderer/Material.h"

#include "MaterialBackend.h"

#include <iterator>
#include <utility>

namespace Engine::Renderer {
struct Material::GpuState {
  nvrhi::BufferHandle ConstantBuffer;
  nvrhi::BindingSetHandle BindingSet;
  nvrhi::TextureHandle NdfResidualTexture;
  nvrhi::TextureHandle NdfDistributionTexture;
  std::shared_ptr<const PrismNdfData> UploadedNdf;
};

Material::~Material() = default;

nvrhi::BufferHandle MaterialBackendAccess::GetConstantBuffer(const Material& material) {
  return material.m_GpuState->ConstantBuffer;
}

void MaterialBackendAccess::SetConstantBuffer(Material& material, nvrhi::BufferHandle buffer) {
  material.m_GpuState->ConstantBuffer = std::move(buffer);
}

nvrhi::BindingSetHandle MaterialBackendAccess::GetBindingSet(const Material& material) {
  return material.m_GpuState->BindingSet;
}

nvrhi::TextureHandle MaterialBackendAccess::GetNdfResidualTexture(const Material& material) {
  return material.m_GpuState->NdfResidualTexture;
}

void MaterialBackendAccess::SetNdfResidualTexture(Material& material, nvrhi::TextureHandle texture) {
  material.m_GpuState->NdfResidualTexture = std::move(texture);
}

nvrhi::TextureHandle MaterialBackendAccess::GetNdfDistributionTexture(const Material& material) {
  return material.m_GpuState->NdfDistributionTexture;
}

void MaterialBackendAccess::SetNdfDistributionTexture(Material& material, nvrhi::TextureHandle texture) {
  material.m_GpuState->NdfDistributionTexture = std::move(texture);
}

auto MaterialBackendAccess::GetUploadedNdf(const Material& material) -> std::shared_ptr<const PrismNdfData> {
  return material.m_GpuState->UploadedNdf;
}

void MaterialBackendAccess::SetUploadedNdf(Material& material, std::shared_ptr<const PrismNdfData> payload) {
  material.m_GpuState->UploadedNdf = std::move(payload);
}

void MaterialBackendAccess::SetBindingSet(Material& material, nvrhi::BindingSetHandle bindingSet) {
  material.m_GpuState->BindingSet = std::move(bindingSet);
}

namespace {
bool Equal(const DirectX::XMFLOAT4& left, const DirectX::XMFLOAT4& right) {
  return left.x == right.x && left.y == right.y && left.z == right.z && left.w == right.w;
}

bool Equal(const DirectX::XMUINT4& left, const DirectX::XMUINT4& right) {
  return left.x == right.x && left.y == right.y && left.z == right.z && left.w == right.w;
}

bool Equal(const MaterialGpu& left, const MaterialGpu& right) {
  if (!Equal(left.BaseColor, right.BaseColor) || !Equal(left.EmissiveMetallic, right.EmissiveMetallic) ||
      !Equal(left.SurfaceParams, right.SurfaceParams) || !Equal(left.SpecularParams, right.SpecularParams) ||
      !Equal(left.ModelFlags, right.ModelFlags) || !Equal(left.AlphaParams, right.AlphaParams) ||
      !Equal(left.TextureCoordinates0, right.TextureCoordinates0) ||
      !Equal(left.TextureCoordinates1, right.TextureCoordinates1) || !Equal(left.TextureParams, right.TextureParams)) {
    return false;
  }
  for (size_t index = 0; index < std::size(left.TextureTransformRows); ++index) {
    if (!Equal(left.TextureTransformRows[index], right.TextureTransformRows[index]) ||
        !Equal(left.TextureTransformOffsets[index], right.TextureTransformOffsets[index])) {
      return false;
    }
  }
  for (size_t index = 0; index < std::size(left.NdfCoefficients); ++index) {
    if (!Equal(left.NdfCoefficients[index], right.NdfCoefficients[index])) {
      return false;
    }
  }
  if (!Equal(left.NdfParams, right.NdfParams)) {
    return false;
  }
  return true;
}
} // namespace

Material::Material(uint32_t materialID)
    : m_MaterialID(materialID), m_GpuState(std::make_unique<GpuState>()), m_Constants(BuildMaterialGpu({})) {}

void Material::SetProperties(const MaterialProperties& properties) {
  if (properties.GeneratedNdf && properties.GeneratedNdf->residuals.size() != PrismNdfSampleCount) {
    return;
  }
  const MaterialGpu constants = BuildMaterialGpu(properties);
  if (properties.GeneratedNdf != m_GeneratedNdf) {
    m_GeneratedNdf = properties.GeneratedNdf;
    m_IsDirty = true;
  }
  if (properties.TextureAnisotropyLevels != m_TextureAnisotropyLevels) {
    m_TextureAnisotropyLevels = properties.TextureAnisotropyLevels;
    m_IsDirty = true;
  }
  if (!Equal(constants, m_Constants)) {
    m_Constants = constants;
    m_IsDirty = true;
  }
}

void Material::SetColor(const DirectX::XMFLOAT4& color) {
  MaterialProperties properties = GetProperties();
  properties.BaseColor = color;
  SetProperties(properties);
}

void Material::SetEmissive(const DirectX::XMFLOAT3& emissive) {
  MaterialProperties properties = GetProperties();
  properties.Emissive = emissive;
  SetProperties(properties);
}

void Material::SetMetallic(float metallic) {
  MaterialProperties properties = GetProperties();
  properties.Metallic = metallic;
  SetProperties(properties);
}

void Material::SetRoughness(float roughness) {
  MaterialProperties properties = GetProperties();
  properties.Roughness = roughness;
  SetProperties(properties);
}

MaterialProperties Material::GetProperties() const {
  MaterialProperties properties;
  properties.BaseColor = m_Constants.BaseColor;
  properties.Emissive = {m_Constants.EmissiveMetallic.x, m_Constants.EmissiveMetallic.y,
                         m_Constants.EmissiveMetallic.z};
  properties.Metallic = m_Constants.EmissiveMetallic.w;
  properties.Roughness = m_Constants.SurfaceParams.x;
  properties.GtrGamma = m_Constants.SurfaceParams.z;
  properties.AnisotropicRoughnessU = m_Constants.SpecularParams.w;
  properties.AnisotropicRoughnessV = m_Constants.AlphaParams.y;
  properties.Ior = m_Constants.SurfaceParams.w;
  properties.SpecularF0 = {m_Constants.SpecularParams.x, m_Constants.SpecularParams.y, m_Constants.SpecularParams.z};
  properties.Model = static_cast<BrdfModel>(m_Constants.ModelFlags.y);
  properties.GeneratedNdf = m_GeneratedNdf;
  properties.SpecularMode = static_cast<DielectricSpecularMode>(m_Constants.ModelFlags.z & 0xffu);
  properties.Workflow = (m_Constants.ModelFlags.z & 0x100u) != 0 ? MaterialWorkflow::SpecularGlossiness
                                                                 : MaterialWorkflow::MetallicRoughness;
  properties.AlphaMode = static_cast<MaterialAlphaMode>(m_Constants.ModelFlags.w & 0xffu);
  properties.DoubleSided = (m_Constants.ModelFlags.w & 0x100u) != 0;
  properties.AlphaCutoff = m_Constants.AlphaParams.x;
  properties.BaseColorTexCoord = m_Constants.TextureCoordinates0.x;
  properties.NormalTexCoord = m_Constants.TextureCoordinates0.y;
  properties.MetallicRoughnessTexCoord = m_Constants.TextureCoordinates0.z;
  properties.OcclusionTexCoord = m_Constants.TextureCoordinates0.w;
  properties.EmissiveTexCoord = m_Constants.TextureCoordinates1.x;
  properties.NormalScale = m_Constants.TextureParams.x;
  properties.OcclusionStrength = m_Constants.TextureParams.y;
  for (size_t i = 0; i < 5; ++i) {
    const auto& rows = m_Constants.TextureTransformRows[i];
    const auto& offset = m_Constants.TextureTransformOffsets[i];
    auto& transform = properties.TextureTransforms[i];
    transform.Offset = {offset.x, offset.y};
    transform.Scale.x = std::sqrt(rows.x * rows.x + rows.z * rows.z);
    transform.Scale.y = transform.Scale.x > 0.0f ? (rows.x * rows.w - rows.y * rows.z) / transform.Scale.x : 0.0f;
    transform.Rotation = std::atan2(rows.z, rows.x);
  }
  properties.TextureAnisotropyLevels = m_TextureAnisotropyLevels;
  return properties;
}
} // namespace Engine::Renderer

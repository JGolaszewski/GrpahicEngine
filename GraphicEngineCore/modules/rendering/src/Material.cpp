#include "rendering/include/renderer/Material.h"

namespace Engine::Renderer {
namespace {
auto Equal(const DirectX::XMFLOAT4& left, const DirectX::XMFLOAT4& right) noexcept -> bool {
  return left.x == right.x && left.y == right.y && left.z == right.z && left.w == right.w;
}

auto Equal(const DirectX::XMUINT4& left, const DirectX::XMUINT4& right) noexcept -> bool {
  return left.x == right.x && left.y == right.y && left.z == right.z && left.w == right.w;
}

auto Equal(const MaterialGpu& left, const MaterialGpu& right) noexcept -> bool {
  if (!Equal(left.BaseColor, right.BaseColor) || !Equal(left.EmissiveMetallic, right.EmissiveMetallic) ||
      !Equal(left.SurfaceParams, right.SurfaceParams) || !Equal(left.SpecularParams, right.SpecularParams) ||
      !Equal(left.ModelFlags, right.ModelFlags) || !Equal(left.AlphaParams, right.AlphaParams) ||
      !Equal(left.TextureCoordinates0, right.TextureCoordinates0) ||
      !Equal(left.TextureCoordinates1, right.TextureCoordinates1) || !Equal(left.TextureParams, right.TextureParams)) {
    return false;
  }
  for (size_t index = 0; index < 5; ++index) {
    if (!Equal(left.TextureTransformRows[index], right.TextureTransformRows[index]) ||
        !Equal(left.TextureTransformOffsets[index], right.TextureTransformOffsets[index])) {
      return false;
    }
  }
  return true;
}
} // namespace
Material::Material(uint32_t materialID) : m_MaterialID(materialID), m_Constants(BuildMaterialGpu({})) {}

void Material::SetProperties(const MaterialProperties& properties) {
  const MaterialGpu constants = BuildMaterialGpu(properties);
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

auto Material::GetProperties() const -> MaterialProperties {
  MaterialProperties properties;
  properties.BaseColor = m_Constants.BaseColor;
  properties.Emissive = {m_Constants.EmissiveMetallic.x, m_Constants.EmissiveMetallic.y,
                         m_Constants.EmissiveMetallic.z};
  properties.Metallic = m_Constants.EmissiveMetallic.w;
  properties.Roughness = m_Constants.SurfaceParams.x;
  properties.GtrGamma = m_Constants.SurfaceParams.z;
  properties.Ior = m_Constants.SurfaceParams.w;
  properties.SpecularF0 = {m_Constants.SpecularParams.x, m_Constants.SpecularParams.y, m_Constants.SpecularParams.z};
  properties.Model = static_cast<BrdfModel>(m_Constants.ModelFlags.y);
  properties.SpecularMode = static_cast<DielectricSpecularMode>(m_Constants.ModelFlags.z & 0xffU);
  properties.Workflow = (m_Constants.ModelFlags.z & 0x100U) != 0 ? MaterialWorkflow::SpecularGlossiness
                                                                 : MaterialWorkflow::MetallicRoughness;
  properties.AlphaMode = static_cast<MaterialAlphaMode>(m_Constants.ModelFlags.w & 0xffU);
  properties.DoubleSided = (m_Constants.ModelFlags.w & 0x100U) != 0;
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
    transform.Scale.x = std::sqrt((rows.x * rows.x) + (rows.z * rows.z));
    transform.Scale.y = transform.Scale.x > 0.0F ? ((rows.x * rows.w) - (rows.y * rows.z)) / transform.Scale.x : 0.0F;
    transform.Rotation = std::atan2(rows.z, rows.x);
  }
  return properties;
}
} // namespace Engine::Renderer

#pragma once

#include "runtime/include/CoreExport.h"
#include "runtime/include/PrismNdfData.h"
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Renderer {
struct TextureResource;
class MaterialBackendAccess;
} // namespace Engine::Renderer

namespace Engine::Renderer {

/// BRDF family selected for shading a material.
enum class BrdfModel : uint32_t {
  GGX = 1,
  GTR = 2,
  Beckmann = 3,
  Gauss = 4,
  BlinnPhong = 5,
  AshikhminShirley = 6,
  Schlick = 7,
  Sawicki = 8,
  PrismNdf = 9
};

/// Metallic-roughness or specular-glossiness material parameterization.
enum class MaterialWorkflow : uint32_t { MetallicRoughness = 0, SpecularGlossiness = 1 };

/// Source used to derive a dielectric material's normal-incidence reflectance.
enum class DielectricSpecularMode : uint32_t { IndexOfRefraction = 0, ExplicitF0 = 1 };

/// Alpha blending behavior selected for a material.
enum class MaterialAlphaMode : uint32_t { Opaque = 0, Mask = 1, Blend = 2 };

/// Versioned CPU-to-shader material constant-buffer layout.
/// The buffer occupies b1; texture and sampler registers are listed in docs/material-binding-abi.md.
inline constexpr uint32_t MaterialGpuAbiVersion = 6;
/// Minimum alpha used by the shader to avoid a zero roughness denominator.
inline constexpr float MaterialMinAlpha = 1.0e-3f;
/// Smallest supported GTR distribution gamma.
inline constexpr float MaterialMinGtrGamma = 1.0f;
/// Largest supported GTR distribution gamma.
inline constexpr float MaterialMaxGtrGamma = 8.0f;
/// Smallest supported index of refraction.
inline constexpr float MaterialMinIor = 1.0f;
/// Largest supported index of refraction.
inline constexpr float MaterialMaxIor = 3.0f;

/// Packed material parameters matching the shader constant-buffer ABI.
struct MaterialGpu {
  DirectX::XMFLOAT4 BaseColor;               ///< Linear RGBA base color factor.
  DirectX::XMFLOAT4 EmissiveMetallic;        ///< RGB emissive factor and metallic value.
  DirectX::XMFLOAT4 SurfaceParams;           ///< Roughness, alpha floor, GTR gamma, and IOR.
  DirectX::XMFLOAT4 SpecularParams;          ///< RGB dielectric F0 and Ashikhmin-Shirley U roughness.
  DirectX::XMUINT4 ModelFlags;               ///< ABI version, BRDF, workflow/specular, and alpha/double-sided flags.
  DirectX::XMFLOAT4 AlphaParams;             ///< Alpha cutoff, Ashikhmin-Shirley V roughness, and reserved values.
  DirectX::XMUINT4 TextureCoordinates0;      ///< UV indices for base color, normal, metallic-roughness, and occlusion.
  DirectX::XMUINT4 TextureCoordinates1;      ///< UV index for emission and reserved values.
  DirectX::XMFLOAT4 TextureParams;           ///< Normal scale, occlusion strength, and reserved values.
  DirectX::XMFLOAT4 TextureTransformRows[5]; ///< Two UV transform rows for each material texture slot.
  DirectX::XMFLOAT4 TextureTransformOffsets[5]; ///< UV offsets for each material texture slot.
  DirectX::XMFLOAT4 NdfCoefficients[12];        ///< 45 degree-8 real Zernike coefficients, packed as float4 values.
  DirectX::XMFLOAT4 NdfParams;                  ///< Projected-energy normalization and reserved values.
};

static_assert(sizeof(DirectX::XMFLOAT4) == 16);
static_assert(sizeof(DirectX::XMUINT4) == 16);
static_assert(offsetof(MaterialGpu, BaseColor) == 0);
static_assert(offsetof(MaterialGpu, EmissiveMetallic) == 16);
static_assert(offsetof(MaterialGpu, SurfaceParams) == 32);
static_assert(offsetof(MaterialGpu, SpecularParams) == 48);
static_assert(offsetof(MaterialGpu, ModelFlags) == 64);
static_assert(offsetof(MaterialGpu, AlphaParams) == 80);
static_assert(offsetof(MaterialGpu, TextureCoordinates0) == 96);
static_assert(offsetof(MaterialGpu, TextureCoordinates1) == 112);
static_assert(offsetof(MaterialGpu, TextureParams) == 128);
static_assert(offsetof(MaterialGpu, TextureTransformRows) == 144);
static_assert(offsetof(MaterialGpu, TextureTransformOffsets) == 224);
static_assert(offsetof(MaterialGpu, NdfCoefficients) == 304);
static_assert(offsetof(MaterialGpu, NdfParams) == 496);
static_assert(sizeof(MaterialGpu) == 512);

/// UV offset, scale, and rotation for one material texture slot.
struct MaterialTextureTransform {
  DirectX::XMFLOAT2 Offset = {0.0f, 0.0f}; ///< UV translation.
  DirectX::XMFLOAT2 Scale = {1.0f, 1.0f};  ///< Per-axis UV scale.
  float Rotation = 0.0f;                   ///< UV rotation in radians.
};

/// Anisotropy request for one material texture slot. GlobalCap follows the active RenderSystem maximum.
enum class TextureAnisotropy : uint32_t { GlobalCap = 0, X1 = 1, X2 = 2, X4 = 4, X8 = 8, X16 = 16 };

/// Maximum anisotropy accepted by the renderer-wide texture filtering setting.
inline constexpr uint32_t DefaultMaxTextureAnisotropy = 8;
/// Returns true when a value is one of the supported hardware levels.
inline constexpr bool IsSupportedTextureAnisotropy(uint32_t value) noexcept {
  return value == 1 || value == 2 || value == 4 || value == 8 || value == 16;
}
/// Resolves a slot request to the active renderer maximum.
inline constexpr uint32_t ResolveTextureAnisotropy(TextureAnisotropy request, uint32_t engineMaximum) noexcept {
  const uint32_t requested = static_cast<uint32_t>(request);
  const uint32_t validMaximum =
      IsSupportedTextureAnisotropy(engineMaximum) ? engineMaximum : DefaultMaxTextureAnisotropy;
  const uint32_t validRequest =
      request == TextureAnisotropy::GlobalCap || !IsSupportedTextureAnisotropy(requested) ? validMaximum : requested;
  return (std::min)(validRequest, validMaximum);
}

/// Editable material factors and UV set selections before GPU packing.
struct MaterialProperties {
  DirectX::XMFLOAT4 BaseColor = {1.0f, 1.0f, 1.0f, 1.0f}; ///< Linear RGBA base color multiplier.
  DirectX::XMFLOAT3 Emissive = {0.0f, 0.0f, 0.0f};        ///< Linear RGB emission multiplier.
  float Metallic = 0.0f;                                  ///< Metalness in the metallic-roughness workflow.
  float Roughness = 0.5f;                                 ///< Surface microfacet roughness.
  float GtrGamma = 2.0f;                                  ///< GTR distribution gamma parameter.
  float AnisotropicRoughnessU = 0.5f;                     ///< Ashikhmin-Shirley roughness along the tangent axis.
  float AnisotropicRoughnessV = 0.5f;                     ///< Ashikhmin-Shirley roughness along the bitangent axis.
  float Ior = 1.5f;                                       ///< Index of refraction used to derive dielectric F0.
  DirectX::XMFLOAT3 SpecularF0 = {0.04f, 0.04f, 0.04f};   ///< Explicit dielectric normal-incidence reflectance.
  BrdfModel Model = BrdfModel::GGX;                       ///< BRDF distribution used by the shader.
  MaterialWorkflow Workflow = MaterialWorkflow::MetallicRoughness;                 ///< Active material workflow.
  DielectricSpecularMode SpecularMode = DielectricSpecularMode::IndexOfRefraction; ///< Source for dielectric F0.
  MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;                         ///< Alpha test or blend mode.
  float AlphaCutoff = 0.5f;                                                        ///< Alpha threshold in mask mode.
  bool DoubleSided = false;                      ///< Whether both sides of triangles are rendered.
  uint32_t BaseColorTexCoord = 0;                ///< UV set sampled for base color.
  uint32_t NormalTexCoord = 0;                   ///< UV set sampled for the normal map.
  uint32_t MetallicRoughnessTexCoord = 0;        ///< UV set sampled for metallic-roughness.
  uint32_t OcclusionTexCoord = 0;                ///< UV set sampled for occlusion.
  uint32_t EmissiveTexCoord = 0;                 ///< UV set sampled for emission.
  float NormalScale = 1.0f;                      ///< Multiplier applied to sampled normal XY components.
  float OcclusionStrength = 1.0f;                ///< Blend weight for the occlusion map.
  MaterialTextureTransform TextureTransforms[5]; ///< Transforms for base color, metallic-roughness, normal,
                                                 ///< occlusion, and emissive maps.
  std::array<TextureAnisotropy, 5> TextureAnisotropyLevels{}; ///< Per-slot sampler requests; defaults to GlobalCap.
  std::shared_ptr<const PrismNdfData> GeneratedNdf; ///< Immutable generated payload; absent for analytic distributions.
};

inline float ClampFinite(float value, float fallback, float minimum, float maximum) noexcept {
  return std::isfinite(value) ? (std::clamp)(value, minimum, maximum) : fallback;
}

inline float FiniteNonNegative(float value) noexcept { return std::isfinite(value) ? (std::max)(value, 0.0f) : 0.0f; }

inline MaterialGpu BuildMaterialGpu(const MaterialProperties& properties) noexcept {
  const float roughness = ClampFinite(properties.Roughness, 0.5f, 0.0f, 1.0f);
  const float ior = ClampFinite(properties.Ior, 1.5f, MaterialMinIor, MaterialMaxIor);
  const auto validModel = [](BrdfModel model) {
    return static_cast<uint32_t>(model) >= static_cast<uint32_t>(BrdfModel::GGX) &&
                   static_cast<uint32_t>(model) <= static_cast<uint32_t>(BrdfModel::PrismNdf)
               ? model
               : BrdfModel::GGX;
  };
  const auto validWorkflow = [](MaterialWorkflow workflow) {
    return static_cast<uint32_t>(workflow) <= static_cast<uint32_t>(MaterialWorkflow::SpecularGlossiness)
               ? workflow
               : MaterialWorkflow::MetallicRoughness;
  };
  const auto validSpecularMode = [](DielectricSpecularMode mode) {
    return static_cast<uint32_t>(mode) <= static_cast<uint32_t>(DielectricSpecularMode::ExplicitF0)
               ? mode
               : DielectricSpecularMode::IndexOfRefraction;
  };
  const auto validAlphaMode = [](MaterialAlphaMode mode) {
    return static_cast<uint32_t>(mode) <= static_cast<uint32_t>(MaterialAlphaMode::Blend) ? mode
                                                                                          : MaterialAlphaMode::Opaque;
  };

  MaterialGpu gpu{
      {ClampFinite(properties.BaseColor.x, 1.0f, 0.0f, 1.0f), ClampFinite(properties.BaseColor.y, 1.0f, 0.0f, 1.0f),
       ClampFinite(properties.BaseColor.z, 1.0f, 0.0f, 1.0f), ClampFinite(properties.BaseColor.w, 1.0f, 0.0f, 1.0f)},
      {FiniteNonNegative(properties.Emissive.x), FiniteNonNegative(properties.Emissive.y),
       FiniteNonNegative(properties.Emissive.z), ClampFinite(properties.Metallic, 0.0f, 0.0f, 1.0f)},
      {roughness, (std::max)(roughness * roughness, MaterialMinAlpha),
       ClampFinite(properties.GtrGamma, 2.0f, MaterialMinGtrGamma, MaterialMaxGtrGamma), ior},
      {ClampFinite(properties.SpecularF0.x, 0.04f, 0.0f, 1.0f), ClampFinite(properties.SpecularF0.y, 0.04f, 0.0f, 1.0f),
       ClampFinite(properties.SpecularF0.z, 0.04f, 0.0f, 1.0f),
       ClampFinite(properties.AnisotropicRoughnessU, 0.5f, 0.0f, 1.0f)},
      {MaterialGpuAbiVersion, static_cast<uint32_t>(validModel(properties.Model)),
       static_cast<uint32_t>(validSpecularMode(properties.SpecularMode)) |
           (validWorkflow(properties.Workflow) == MaterialWorkflow::SpecularGlossiness ? 0x100u : 0u),
       static_cast<uint32_t>(validAlphaMode(properties.AlphaMode)) | (properties.DoubleSided ? 0x100u : 0u)},
      {ClampFinite(properties.AlphaCutoff, 0.5f, 0.0f, 1.0f),
       ClampFinite(properties.AnisotropicRoughnessV, 0.5f, 0.0f, 1.0f), 0.0f, 0.0f},
      {},
      {},
      {},
      {},
      {},
      {},
      {1.0f, 0.0f, 0.0f, 0.0f}};
  gpu.TextureCoordinates0 = {
      properties.BaseColorTexCoord < MaxTextureCoordinateSets ? properties.BaseColorTexCoord : 0,
      properties.NormalTexCoord < MaxTextureCoordinateSets ? properties.NormalTexCoord : 0,
      properties.MetallicRoughnessTexCoord < MaxTextureCoordinateSets ? properties.MetallicRoughnessTexCoord : 0,
      properties.OcclusionTexCoord < MaxTextureCoordinateSets ? properties.OcclusionTexCoord : 0};
  gpu.TextureCoordinates1 = {properties.EmissiveTexCoord < MaxTextureCoordinateSets ? properties.EmissiveTexCoord : 0,
                             0, 0, 0};
  gpu.TextureParams = {ClampFinite(properties.NormalScale, 1.0f, 0.0f, 1.0e6f),
                       ClampFinite(properties.OcclusionStrength, 1.0f, 0.0f, 1.0f), 0.0f, 0.0f};
  for (size_t i = 0; i < 5; ++i) {
    const auto& transform = properties.TextureTransforms[i];
    const float rotation = std::isfinite(transform.Rotation) ? transform.Rotation : 0.0f;
    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);
    const float scaleX = std::isfinite(transform.Scale.x) ? transform.Scale.x : 1.0f;
    const float scaleY = std::isfinite(transform.Scale.y) ? transform.Scale.y : 1.0f;
    gpu.TextureTransformRows[i] = {cosine * scaleX, -sine * scaleY, sine * scaleX, cosine * scaleY};
    gpu.TextureTransformOffsets[i] = {std::isfinite(transform.Offset.x) ? transform.Offset.x : 0.0f,
                                      std::isfinite(transform.Offset.y) ? transform.Offset.y : 0.0f, 0.0f, 0.0f};
  }
  if (properties.GeneratedNdf) {
    for (size_t index = 0; index < PrismNdfCoefficientCount; ++index) {
      auto& packed = gpu.NdfCoefficients[index / 4];
      const std::array<float*, 4> components{&packed.x, &packed.y, &packed.z, &packed.w};
      *components[index % 4] = std::isfinite(properties.GeneratedNdf->zernikeCoefficients[index])
                                   ? properties.GeneratedNdf->zernikeCoefficients[index]
                                   : 0.0F;
    }
    gpu.NdfParams.x =
        std::isfinite(properties.GeneratedNdf->normalization) && properties.GeneratedNdf->normalization > 0.0F
            ? properties.GeneratedNdf->normalization
            : 1.0F;
  }
  return gpu;
}

static_assert(std::is_trivially_copyable_v<MaterialGpu>);

/// CPU-owned material properties, texture references, and dirty GPU state.
class ENGINE_API Material {
  friend class MaterialSystem;
  friend class MaterialBackendAccess;

public:
  /// Creates a material with the supplied engine-assigned identifier.
  /// @param materialID Unique material identifier.
  Material(uint32_t materialID);

  /// Releases the material's private GPU state.
  ~Material();

  /// Replaces all editable factors and marks the material dirty for upload. A nonnull PRISM payload with an invalid
  /// residual sample count is rejected without changing the material.
  /// @param properties New material factors and texture-coordinate selections.
  void SetProperties(const MaterialProperties& properties);

  /// Sets the linear RGBA base color factor.
  /// @param color New base color.
  void SetColor(const DirectX::XMFLOAT4& color);

  /// Sets the linear RGB emissive factor.
  /// @param emissive New emissive color.
  void SetEmissive(const DirectX::XMFLOAT3& emissive);

  /// Sets the metallic factor.
  /// @param metallic New metallic value.
  void SetMetallic(float metallic);

  /// Sets the surface roughness factor.
  /// @param roughness New roughness value.
  void SetRoughness(float roughness);

  /// Marks the material's GPU constants as synchronized.
  inline void ClearDirtyFlag() { m_IsDirty = false; }
  /// Reports whether material constants need to be uploaded.
  /// @return True when properties changed since the last upload.
  inline bool IsDirty() const { return m_IsDirty; }

  /// Returns a copy of the editable material properties.
  /// @return Current material factors and UV selections.
  MaterialProperties GetProperties() const;

  /// Reports whether any texture slot has an explicit override.
  /// @return True when at least one texture differs from its imported default.
  bool HasTextureOverrides() const noexcept { return m_HasTextureOverrides; }
  /// Returns the currently bound base-color texture.
  /// @return Shared texture resource, or empty when unavailable.
  const std::shared_ptr<TextureResource>& GetBaseColorTexture() const noexcept { return m_BaseColorTexture; }
  /// Returns the currently bound normal texture.
  /// @return Shared texture resource, or empty when unavailable.
  const std::shared_ptr<TextureResource>& GetNormalTexture() const noexcept { return m_NormalTexture; }
  /// Returns the currently bound metallic-roughness texture.
  /// @return Shared texture resource, or empty when unavailable.
  const std::shared_ptr<TextureResource>& GetMetallicRoughnessTexture() const noexcept {
    return m_MetallicRoughnessTexture;
  }
  /// Returns the currently bound occlusion texture.
  /// @return Shared texture resource, or empty when unavailable.
  const std::shared_ptr<TextureResource>& GetOcclusionTexture() const noexcept { return m_OcclusionTexture; }
  /// Returns the currently bound emissive texture.
  /// @return Shared texture resource, or empty when unavailable.
  const std::shared_ptr<TextureResource>& GetEmissiveTexture() const noexcept { return m_EmissiveTexture; }
  /// Returns the imported default base-color texture.
  /// @return Shared default resource, or empty when no imported default exists.
  const std::shared_ptr<TextureResource>& GetDefaultBaseColorTexture() const noexcept {
    return m_DefaultBaseColorTexture;
  }
  /// Returns the imported default normal texture.
  /// @return Shared default resource, or empty when no imported default exists.
  const std::shared_ptr<TextureResource>& GetDefaultNormalTexture() const noexcept { return m_DefaultNormalTexture; }
  /// Returns the asset identifier of the selected base-color texture.
  /// @return Texture identifier, or empty when the slot has no asset reference.
  const std::optional<std::string>& GetBaseColorTextureGuid() const noexcept { return m_BaseColorTextureGuid; }
  /// Returns the asset identifier of the selected normal texture.
  /// @return Texture identifier, or empty when the slot has no asset reference.
  const std::optional<std::string>& GetNormalTextureGuid() const noexcept { return m_NormalTextureGuid; }

private:
  struct GpuState;

  bool m_IsDirty = true;
  uint32_t m_MaterialID;

  std::unique_ptr<GpuState> m_GpuState;
  std::shared_ptr<const PrismNdfData> m_GeneratedNdf;
  std::shared_ptr<TextureResource> m_BaseColorTexture;
  std::shared_ptr<TextureResource> m_NormalTexture;
  std::shared_ptr<TextureResource> m_MetallicRoughnessTexture;
  std::shared_ptr<TextureResource> m_OcclusionTexture;
  std::shared_ptr<TextureResource> m_EmissiveTexture;
  std::shared_ptr<TextureResource> m_DefaultBaseColorTexture;
  std::shared_ptr<TextureResource> m_DefaultNormalTexture;
  bool m_HasTextureOverrides = false;
  std::optional<std::string> m_BaseColorTextureGuid;
  std::optional<std::string> m_NormalTextureGuid;
  std::array<TextureAnisotropy, 5> m_TextureAnisotropyLevels{};

  MaterialGpu m_Constants;
};
} // namespace Engine::Renderer

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

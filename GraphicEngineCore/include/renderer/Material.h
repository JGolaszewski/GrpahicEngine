#pragma once

#include "CoreExport.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <nvrhi/nvrhi.h>
#include <optional>
#include <string>
#include <type_traits>

namespace Renderer
{
struct TextureResource;
}

namespace Renderer
{

enum class BrdfModel : uint32_t
{
    Legacy = 0,
    GGX = 1,
    GTR = 2
};

enum class DielectricSpecularMode : uint32_t
{
    IndexOfRefraction = 0,
    ExplicitF0 = 1
};

enum class MaterialAlphaMode : uint32_t
{
    Opaque = 0,
    Mask = 1,
    Blend = 2
};

// The material buffer occupies b1; texture and sampler registers are listed in docs/material-binding-abi.md.
inline constexpr uint32_t MaterialGpuAbiVersion = 3;
inline constexpr float MaterialMinAlpha = 1.0e-3f;
inline constexpr float MaterialMinGtrGamma = 1.0f;
inline constexpr float MaterialMaxGtrGamma = 8.0f;
inline constexpr float MaterialMinIor = 1.0f;
inline constexpr float MaterialMaxIor = 3.0f;

struct MaterialGpu
{
    DirectX::XMFLOAT4 BaseColor;
    DirectX::XMFLOAT4 EmissiveMetallic;
    DirectX::XMFLOAT4 SurfaceParams;
    DirectX::XMFLOAT4 SpecularParams;
    DirectX::XMUINT4 ModelFlags;
    DirectX::XMFLOAT4 AlphaParams;
    DirectX::XMUINT4 TextureCoordinates0;
    DirectX::XMUINT4 TextureCoordinates1;
    DirectX::XMFLOAT4 TextureParams;
    DirectX::XMFLOAT4 TextureTransformRows[5];
    DirectX::XMFLOAT4 TextureTransformOffsets[5];
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
static_assert(sizeof(MaterialGpu) == 304);

struct MaterialTextureTransform
{
    DirectX::XMFLOAT2 Offset = {0.0f, 0.0f};
    DirectX::XMFLOAT2 Scale = {1.0f, 1.0f};
    float Rotation = 0.0f;
};

struct MaterialProperties
{
    DirectX::XMFLOAT4 BaseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT3 Emissive = {0.0f, 0.0f, 0.0f};
    float Metallic = 0.0f;
    float Roughness = 0.5f;
    float GtrGamma = 2.0f;
    float Ior = 1.5f;
    DirectX::XMFLOAT3 SpecularF0 = {0.04f, 0.04f, 0.04f};
    BrdfModel Model = BrdfModel::Legacy;
    DielectricSpecularMode SpecularMode = DielectricSpecularMode::IndexOfRefraction;
    MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
    float AlphaCutoff = 0.5f;
    bool DoubleSided = false;
    uint32_t BaseColorTexCoord = 0;
    uint32_t NormalTexCoord = 0;
    uint32_t MetallicRoughnessTexCoord = 0;
    uint32_t OcclusionTexCoord = 0;
    uint32_t EmissiveTexCoord = 0;
    float NormalScale = 1.0f;
    float OcclusionStrength = 1.0f;
    MaterialTextureTransform TextureTransforms[5];
};

inline float ClampFinite(float value, float fallback, float minimum, float maximum) noexcept
{
    return std::isfinite(value) ? (std::clamp)(value, minimum, maximum) : fallback;
}

inline float FiniteNonNegative(float value) noexcept
{
    return std::isfinite(value) ? (std::max)(value, 0.0f) : 0.0f;
}

inline MaterialGpu BuildMaterialGpu(const MaterialProperties &properties) noexcept
{
    const float roughness = ClampFinite(properties.Roughness, 0.5f, 0.0f, 1.0f);
    const float ior = ClampFinite(properties.Ior, 1.5f, MaterialMinIor, MaterialMaxIor);
    const auto validModel = [](BrdfModel model) {
        return static_cast<uint32_t>(model) <= static_cast<uint32_t>(BrdfModel::GTR) ? model
                                                                                     : BrdfModel::Legacy;
    };
    const auto validSpecularMode = [](DielectricSpecularMode mode) {
        return static_cast<uint32_t>(mode) <= static_cast<uint32_t>(DielectricSpecularMode::ExplicitF0)
                   ? mode
                   : DielectricSpecularMode::IndexOfRefraction;
    };
    const auto validAlphaMode = [](MaterialAlphaMode mode) {
        return static_cast<uint32_t>(mode) <= static_cast<uint32_t>(MaterialAlphaMode::Blend)
                   ? mode
                   : MaterialAlphaMode::Opaque;
    };

    MaterialGpu gpu{{ClampFinite(properties.BaseColor.x, 1.0f, 0.0f, 1.0f),
             ClampFinite(properties.BaseColor.y, 1.0f, 0.0f, 1.0f),
             ClampFinite(properties.BaseColor.z, 1.0f, 0.0f, 1.0f),
             ClampFinite(properties.BaseColor.w, 1.0f, 0.0f, 1.0f)},
            {FiniteNonNegative(properties.Emissive.x), FiniteNonNegative(properties.Emissive.y),
             FiniteNonNegative(properties.Emissive.z), ClampFinite(properties.Metallic, 0.0f, 0.0f, 1.0f)},
            {roughness, (std::max)(roughness * roughness, MaterialMinAlpha),
             ClampFinite(properties.GtrGamma, 2.0f, MaterialMinGtrGamma, MaterialMaxGtrGamma), ior},
            {ClampFinite(properties.SpecularF0.x, 0.04f, 0.0f, 1.0f),
             ClampFinite(properties.SpecularF0.y, 0.04f, 0.0f, 1.0f),
             ClampFinite(properties.SpecularF0.z, 0.04f, 0.0f, 1.0f), 0.0f},
            {MaterialGpuAbiVersion, static_cast<uint32_t>(validModel(properties.Model)),
             static_cast<uint32_t>(validSpecularMode(properties.SpecularMode)),
             static_cast<uint32_t>(validAlphaMode(properties.AlphaMode)) |
                 (properties.DoubleSided ? 0x100u : 0u)},
            {ClampFinite(properties.AlphaCutoff, 0.5f, 0.0f, 1.0f), 0.0f, 0.0f, 0.0f}};
    gpu.TextureCoordinates0 = {properties.BaseColorTexCoord <= 1 ? properties.BaseColorTexCoord : 0,
                               properties.NormalTexCoord <= 1 ? properties.NormalTexCoord : 0,
                               properties.MetallicRoughnessTexCoord <= 1 ? properties.MetallicRoughnessTexCoord : 0,
                               properties.OcclusionTexCoord <= 1 ? properties.OcclusionTexCoord : 0};
    gpu.TextureCoordinates1 = {properties.EmissiveTexCoord <= 1 ? properties.EmissiveTexCoord : 0, 0, 0, 0};
    gpu.TextureParams = {ClampFinite(properties.NormalScale, 1.0f, 0.0f, 1.0e6f),
                         ClampFinite(properties.OcclusionStrength, 1.0f, 0.0f, 1.0f), 0.0f, 0.0f};
    for (size_t i = 0; i < 5; ++i)
    {
        const auto &transform = properties.TextureTransforms[i];
        const float rotation = std::isfinite(transform.Rotation) ? transform.Rotation : 0.0f;
        const float cosine = std::cos(rotation);
        const float sine = std::sin(rotation);
        const float scaleX = std::isfinite(transform.Scale.x) ? transform.Scale.x : 1.0f;
        const float scaleY = std::isfinite(transform.Scale.y) ? transform.Scale.y : 1.0f;
        gpu.TextureTransformRows[i] = {cosine * scaleX, -sine * scaleY, sine * scaleX, cosine * scaleY};
        gpu.TextureTransformOffsets[i] = {
            std::isfinite(transform.Offset.x) ? transform.Offset.x : 0.0f,
            std::isfinite(transform.Offset.y) ? transform.Offset.y : 0.0f, 0.0f, 0.0f};
    }
    return gpu;
}

static_assert(std::is_trivially_copyable_v<MaterialGpu>);

class ENGINE_API Material
{
    friend class MaterialSystem;

  public:
    Material(uint32_t materialID);

    void SetProperties(const MaterialProperties &properties);

    void SetColor(const DirectX::XMFLOAT4 &color);
    void SetEmissive(const DirectX::XMFLOAT3 &emissive);
    void SetMetallic(float metallic);
    void SetRoughness(float roughness);

    inline void ClearDirtyFlag()
    {
        m_IsDirty = false;
    }
    inline bool IsDirty() const
    {
        return m_IsDirty;
    }

    MaterialProperties GetProperties() const;
    bool HasTextureOverrides() const noexcept
    {
        return m_HasTextureOverrides;
    }
    const std::shared_ptr<TextureResource> &GetBaseColorTexture() const noexcept
    {
        return m_BaseColorTexture;
    }
    const std::shared_ptr<TextureResource> &GetNormalTexture() const noexcept
    {
        return m_NormalTexture;
    }
    const std::shared_ptr<TextureResource> &GetMetallicRoughnessTexture() const noexcept
    {
        return m_MetallicRoughnessTexture;
    }
    const std::shared_ptr<TextureResource> &GetOcclusionTexture() const noexcept
    {
        return m_OcclusionTexture;
    }
    const std::shared_ptr<TextureResource> &GetEmissiveTexture() const noexcept
    {
        return m_EmissiveTexture;
    }
    const std::shared_ptr<TextureResource> &GetDefaultBaseColorTexture() const noexcept
    {
        return m_DefaultBaseColorTexture;
    }
    const std::shared_ptr<TextureResource> &GetDefaultNormalTexture() const noexcept
    {
        return m_DefaultNormalTexture;
    }
    const std::optional<std::string> &GetBaseColorTextureGuid() const noexcept
    {
        return m_BaseColorTextureGuid;
    }
    const std::optional<std::string> &GetNormalTextureGuid() const noexcept
    {
        return m_NormalTextureGuid;
    }
    inline nvrhi::BindingSetHandle GetBindingSet() const
    {
        return m_BindingSet;
    }

  private:
    bool m_IsDirty = true;
    uint32_t m_MaterialID;

    nvrhi::BufferHandle m_ConstantBuffer;
    nvrhi::BindingSetHandle m_BindingSet;
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

    MaterialGpu m_Constants;
};
} // namespace Renderer

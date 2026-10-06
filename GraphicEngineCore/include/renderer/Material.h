#pragma once

#include "CoreExport.h"
#include <nvrhi/nvrhi.h>
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <memory>
#include <optional>
#include <string>

namespace Renderer { struct TextureResource; }

namespace Renderer {

	enum class BrdfModel : uint32_t {
		Legacy = 0,
		GGX = 1,
		GTR = 2,
		Neural = 3
	};

	enum class DielectricSpecularMode : uint32_t {
		IndexOfRefraction = 0,
		ExplicitF0 = 1
	};

	// The material buffer occupies b1; texture and sampler registers are listed in docs/material-binding-abi.md.
	inline constexpr uint32_t MaterialGpuAbiVersion = 1;
inline constexpr float MaterialMinAlpha = 1.0e-3f;
	inline constexpr float MaterialMinGtrGamma = 1.0f;
	inline constexpr float MaterialMaxGtrGamma = 8.0f;
	inline constexpr float MaterialMinIor = 1.0f;
	inline constexpr float MaterialMaxIor = 3.0f;

	struct MaterialGpu {
		DirectX::XMFLOAT4 BaseColor;
		DirectX::XMFLOAT4 EmissiveMetallic;
		DirectX::XMFLOAT4 SurfaceParams;
		DirectX::XMFLOAT4 SpecularParams;
		DirectX::XMUINT4 ModelFlags;
	};

	static_assert(sizeof(DirectX::XMFLOAT4) == 16);
	static_assert(sizeof(DirectX::XMUINT4) == 16);
	static_assert(offsetof(MaterialGpu, BaseColor) == 0);
	static_assert(offsetof(MaterialGpu, EmissiveMetallic) == 16);
	static_assert(offsetof(MaterialGpu, SurfaceParams) == 32);
	static_assert(offsetof(MaterialGpu, SpecularParams) == 48);
	static_assert(offsetof(MaterialGpu, ModelFlags) == 64);
	static_assert(sizeof(MaterialGpu) == 80);

	struct MaterialProperties {
		DirectX::XMFLOAT4 BaseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		DirectX::XMFLOAT3 Emissive = { 0.0f, 0.0f, 0.0f };
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		float GtrGamma = 2.0f;
		float Ior = 1.5f;
		DirectX::XMFLOAT3 SpecularF0 = { 0.04f, 0.04f, 0.04f };
		BrdfModel Model = BrdfModel::Legacy;
		DielectricSpecularMode SpecularMode = DielectricSpecularMode::IndexOfRefraction;
	};

	inline float ClampFinite(float value, float fallback, float minimum, float maximum) noexcept {
		return std::isfinite(value) ? (std::clamp)(value, minimum, maximum) : fallback;
	}

	inline float FiniteNonNegative(float value) noexcept {
		return std::isfinite(value) ? (std::max)(value, 0.0f) : 0.0f;
	}

	inline MaterialGpu BuildMaterialGpu(const MaterialProperties& properties) noexcept {
		const float roughness = ClampFinite(properties.Roughness, 0.5f, 0.0f, 1.0f);
		const float ior = ClampFinite(properties.Ior, 1.5f, MaterialMinIor, MaterialMaxIor);
		const auto validModel = [](BrdfModel model) {
			return static_cast<uint32_t>(model) <= static_cast<uint32_t>(BrdfModel::Neural) ?
				model : BrdfModel::Legacy;
		};
		const auto validSpecularMode = [](DielectricSpecularMode mode) {
			return static_cast<uint32_t>(mode) <= static_cast<uint32_t>(DielectricSpecularMode::ExplicitF0) ?
				mode : DielectricSpecularMode::IndexOfRefraction;
		};

		return {
			{
				ClampFinite(properties.BaseColor.x, 1.0f, 0.0f, 1.0f),
				ClampFinite(properties.BaseColor.y, 1.0f, 0.0f, 1.0f),
				ClampFinite(properties.BaseColor.z, 1.0f, 0.0f, 1.0f),
				ClampFinite(properties.BaseColor.w, 1.0f, 0.0f, 1.0f)
			},
			{
				FiniteNonNegative(properties.Emissive.x),
				FiniteNonNegative(properties.Emissive.y),
				FiniteNonNegative(properties.Emissive.z),
				ClampFinite(properties.Metallic, 0.0f, 0.0f, 1.0f)
			},
			{
				roughness,
				(std::max)(roughness * roughness, MaterialMinAlpha),
				ClampFinite(properties.GtrGamma, 2.0f, MaterialMinGtrGamma, MaterialMaxGtrGamma),
				ior
			},
			{
				ClampFinite(properties.SpecularF0.x, 0.04f, 0.0f, 1.0f),
				ClampFinite(properties.SpecularF0.y, 0.04f, 0.0f, 1.0f),
				ClampFinite(properties.SpecularF0.z, 0.04f, 0.0f, 1.0f),
				0.0f
			},
			{
				MaterialGpuAbiVersion,
				static_cast<uint32_t>(validModel(properties.Model)),
				static_cast<uint32_t>(validSpecularMode(properties.SpecularMode)),
				0u
		}
		};
	}

	static_assert(std::is_trivially_copyable_v<MaterialGpu>);

	class ENGINE_API Material {
		friend class MaterialSystem;
	
	public:
		Material(uint32_t materialID);

        void SetProperties(const MaterialProperties& properties);

		void SetColor(const DirectX::XMFLOAT4& color);	
		void SetEmissive(const DirectX::XMFLOAT3& emissive);
		void SetMetallic(float metallic);
		void SetRoughness(float roughness);

		inline void ClearDirtyFlag() { m_IsDirty = false; }
		inline bool IsDirty() const { return m_IsDirty; }

	MaterialProperties GetProperties() const;
	bool HasTextureOverrides() const noexcept { return m_HasTextureOverrides; }
	const std::shared_ptr<TextureResource>& GetBaseColorTexture() const noexcept { return m_BaseColorTexture; }
	const std::shared_ptr<TextureResource>& GetNormalTexture() const noexcept { return m_NormalTexture; }
	const std::shared_ptr<TextureResource>& GetDefaultBaseColorTexture() const noexcept { return m_DefaultBaseColorTexture; }
	const std::shared_ptr<TextureResource>& GetDefaultNormalTexture() const noexcept { return m_DefaultNormalTexture; }
	const std::optional<std::string>& GetBaseColorTextureGuid() const noexcept { return m_BaseColorTextureGuid; }
	const std::optional<std::string>& GetNormalTextureGuid() const noexcept { return m_NormalTextureGuid; }
		inline nvrhi::BindingSetHandle GetBindingSet() const { return m_BindingSet; }
	private:
		bool m_IsDirty = true;
		uint32_t m_MaterialID;

		nvrhi::BufferHandle m_ConstantBuffer;
		nvrhi::BindingSetHandle m_BindingSet;
	std::shared_ptr<TextureResource> m_BaseColorTexture;
	std::shared_ptr<TextureResource> m_NormalTexture;
	std::shared_ptr<TextureResource> m_DefaultBaseColorTexture;
	std::shared_ptr<TextureResource> m_DefaultNormalTexture;
	bool m_HasTextureOverrides = false;
	std::optional<std::string> m_BaseColorTextureGuid;
	std::optional<std::string> m_NormalTextureGuid;

		MaterialGpu m_Constants;
	};
}

#include "renderer/Material.h"

#include <algorithm>

namespace Renderer {

	Material::Material(uint32_t materialID)
		: m_MaterialID(materialID) {

		m_Constants = {
			{ 1.0f, 1.0f, 1.0f, 1.0f },
			{ 0.0f, 0.0f, 0.0f, 0.0f },
			{ 0.5f, 0.0f, 0.0f, 0.0f }
		};
	}

	void Material::SetProperties(const MaterialProperties& properties) {
		MaterialConst constants{
			properties.BaseColor,
			{
			properties.Emissive.x,
			properties.Emissive.y,
			properties.Emissive.z,
			std::clamp(properties.Metallic, 0.0f, 1.0f)
			},
			{
			std::clamp(properties.Roughness, 0.0f, 1.0f),
			0.0f,
			0.0f,
			0.0f
			}
		};

		if (constants.BaseColor.x != m_Constants.BaseColor.x ||
			constants.BaseColor.y != m_Constants.BaseColor.y ||
			constants.BaseColor.z != m_Constants.BaseColor.z ||
			constants.BaseColor.w != m_Constants.BaseColor.w ||
			constants.EmissiveMetallic.x != m_Constants.EmissiveMetallic.x ||
			constants.EmissiveMetallic.y != m_Constants.EmissiveMetallic.y ||
			constants.EmissiveMetallic.z != m_Constants.EmissiveMetallic.z ||
			constants.EmissiveMetallic.w != m_Constants.EmissiveMetallic.w ||
			constants.SurfaceParams.x != m_Constants.SurfaceParams.x) {
			m_Constants = constants;
			m_IsDirty = true;
		}
	}

	void Material::SetColor(const DirectX::XMFLOAT4& color) {
		if (m_Constants.BaseColor.x != color.x ||
			m_Constants.BaseColor.y != color.y ||
			m_Constants.BaseColor.z != color.z ||
			m_Constants.BaseColor.w != color.w) {
			m_Constants.BaseColor = color;
			m_IsDirty = true;
		}
	}

	void Material::SetEmissive(const DirectX::XMFLOAT3& emissive) {
		if (m_Constants.EmissiveMetallic.x != emissive.x ||
			m_Constants.EmissiveMetallic.y != emissive.y ||
			m_Constants.EmissiveMetallic.z != emissive.z) {
			m_Constants.EmissiveMetallic.x = emissive.x;
			m_Constants.EmissiveMetallic.y = emissive.y;
			m_Constants.EmissiveMetallic.z = emissive.z;
			m_IsDirty = true;
		}
	}

	void Material::SetMetallic(float metallic) {
		const float clampedMetallic = std::clamp(metallic, 0.0f, 1.0f);
		if (m_Constants.EmissiveMetallic.w != clampedMetallic) {
			m_Constants.EmissiveMetallic.w = clampedMetallic;
			m_IsDirty = true;
		}
	}

	void Material::SetRoughness(float roughness) {
		const float clampedRoughness = std::clamp(roughness, 0.0f, 1.0f);
		if (m_Constants.SurfaceParams.x != clampedRoughness) {
			m_Constants.SurfaceParams.x = clampedRoughness;
			m_IsDirty = true;
		}
	}

	MaterialProperties Material::GetProperties() const {
		MaterialProperties properties;
		properties.BaseColor = m_Constants.BaseColor;
		properties.Emissive = {
			m_Constants.EmissiveMetallic.x,
			m_Constants.EmissiveMetallic.y,
			m_Constants.EmissiveMetallic.z
		};
		properties.Metallic = m_Constants.EmissiveMetallic.w;
		properties.Roughness = m_Constants.SurfaceParams.x;
		return properties;
	}
}

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
		m_Constants.BaseColor = properties.BaseColor;
		m_Constants.EmissiveMetallic = {
			properties.Emissive.x,
			properties.Emissive.y,
			properties.Emissive.z,
			std::clamp(properties.Metallic, 0.0f, 1.0f)
		};
		m_Constants.SurfaceParams = {
			std::clamp(properties.Roughness, 0.0f, 1.0f),
			0.0f,
			0.0f,
			0.0f
		};
	}

	void Material::SetColor(const DirectX::XMFLOAT4& color) {
        m_Constants.BaseColor = color;
	}

	void Material::SetEmissive(const DirectX::XMFLOAT3& emissive) {
		m_Constants.EmissiveMetallic.x = emissive.x;
		m_Constants.EmissiveMetallic.y = emissive.y;
		m_Constants.EmissiveMetallic.z = emissive.z;
	}

	void Material::SetMetallic(float metallic) {
		m_Constants.EmissiveMetallic.w = std::clamp(metallic, 0.0f, 1.0f);
	}

	void Material::SetRoughness(float roughness) {
		m_Constants.SurfaceParams.x = std::clamp(roughness, 0.0f, 1.0f);
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
#include "renderer/Material.h"

#include <cstring>

namespace Renderer {
	Material::Material(uint32_t materialID)
		: m_MaterialID(materialID)
		, m_Constants(BuildMaterialGpu({})) {
	}

	void Material::SetProperties(const MaterialProperties& properties) {
		const MaterialGpu constants = BuildMaterialGpu(properties);
		if (std::memcmp(&constants, &m_Constants, sizeof(MaterialGpu)) != 0) {
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
		properties.Emissive = {
			m_Constants.EmissiveMetallic.x,
			m_Constants.EmissiveMetallic.y,
			m_Constants.EmissiveMetallic.z
		};
		properties.Metallic = m_Constants.EmissiveMetallic.w;
		properties.Roughness = m_Constants.SurfaceParams.x;
		properties.GtrGamma = m_Constants.SurfaceParams.z;
		properties.Ior = m_Constants.SurfaceParams.w;
		properties.SpecularF0 = {
			m_Constants.SpecularParams.x,
			m_Constants.SpecularParams.y,
			m_Constants.SpecularParams.z
		};
		properties.Model = static_cast<BrdfModel>(m_Constants.ModelFlags.y);
		properties.SpecularMode = static_cast<DielectricSpecularMode>(m_Constants.ModelFlags.z);
		return properties;
	}
}

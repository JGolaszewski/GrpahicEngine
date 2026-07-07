#include "renderer/Material.h"

#include <algorithm>

namespace Renderer {

	Material::Material(nvrhi::IDevice* device, nvrhi::ShaderHandle vs, nvrhi::ShaderHandle ps)
		: m_Device(device), m_VS(vs), m_PS(ps) {
		nvrhi::BufferDesc bufferDesc;
		bufferDesc.byteSize = sizeof(MaterialConst);
		bufferDesc.isConstantBuffer = true;
		bufferDesc.isVolatile = true;
		bufferDesc.debugName = "MaterialConstantBuffer";
		m_ConstantBuffer = m_Device->createBuffer(bufferDesc);

		nvrhi::BindingLayoutDesc layoutDesc;
		layoutDesc.visibility = nvrhi::ShaderType::All;
		layoutDesc.bindings = {
			nvrhi::BindingLayoutItem::ConstantBuffer(1)
		};
		m_BindingLayout = m_Device->createBindingLayout(layoutDesc);

		nvrhi::BindingSetDesc setDesc;
		setDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(1, m_ConstantBuffer));
		m_BindingSet = m_Device->createBindingSet(setDesc, m_BindingLayout);
	}

	void Material::UploadConstants(nvrhi::ICommandList* commandList) {
		commandList->writeBuffer(m_ConstantBuffer, &m_Constants, sizeof(m_Constants));
	}

	void Material::SetProperties(const MaterialProperties& properties, nvrhi::ICommandList* commandList) {
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

		UploadConstants(commandList);
	}

	void Material::SetColor(const DirectX::XMFLOAT4& color, nvrhi::ICommandList* commandList) {
        m_Constants.BaseColor = color;
		UploadConstants(commandList);
	}

	void Material::SetEmissive(const DirectX::XMFLOAT3& emissive, nvrhi::ICommandList* commandList) {
		m_Constants.EmissiveMetallic.x = emissive.x;
		m_Constants.EmissiveMetallic.y = emissive.y;
		m_Constants.EmissiveMetallic.z = emissive.z;
		UploadConstants(commandList);
	}

	void Material::SetMetallic(float metallic, nvrhi::ICommandList* commandList) {
		m_Constants.EmissiveMetallic.w = std::clamp(metallic, 0.0f, 1.0f);
		UploadConstants(commandList);
	}

	void Material::SetRoughness(float roughness, nvrhi::ICommandList* commandList) {
		m_Constants.SurfaceParams.x = std::clamp(roughness, 0.0f, 1.0f);
		UploadConstants(commandList);
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

	const Renderer::Material& Material::DefaultMaterial(nvrhi::IDevice* device)
	{
		static const Material defaultMaterial(device, nullptr, nullptr);

		return defaultMaterial;
	}

}
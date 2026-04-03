#include "renderer/Material.h"

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

	void Material::SetColor(const DirectX::XMFLOAT4& color, nvrhi::ICommandList* commandList) {
		MaterialConst data{ color };
		commandList->writeBuffer(m_ConstantBuffer, &data, sizeof(data));
	}
}
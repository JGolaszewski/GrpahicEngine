#include "renderer/MaterialSystem.h"

namespace Renderer {
	MaterialSystem::MaterialSystem(nvrhi::IDevice* device, nvrhi::ShaderHandle standardVS, nvrhi::ShaderHandle standardPS)
		: m_Device(device), m_StandardVS(standardVS), m_StandardPS(standardPS) {
		nvrhi::BindingLayoutDesc layoutDesc;
		layoutDesc.visibility = nvrhi::ShaderType::All;
		layoutDesc.bindings = {
			nvrhi::BindingLayoutItem::ConstantBuffer(1)
		};
		m_StandardLayout = m_Device->createBindingLayout(layoutDesc);
	}

	MaterialSystem::~MaterialSystem() {
		m_ActiveMaterials.clear();
	}

	void MaterialSystem::RegisterMaterial(std::shared_ptr<Material> material) {
		// Allocate constant buffer for the material
		nvrhi::BufferDesc bufferDesc;
		bufferDesc.byteSize = sizeof(MaterialConst);
		bufferDesc.isConstantBuffer = true;
		bufferDesc.isVolatile = false;
		material->m_ConstantBuffer = m_Device->createBuffer(bufferDesc);

		// Create binding set for the material
		nvrhi::BindingSetDesc setDesc;
		setDesc.bindings = {
				nvrhi::BindingSetItem::ConstantBuffer(1, material->m_ConstantBuffer)
		};
		material->m_BindingSet = m_Device->createBindingSet(setDesc, m_StandardLayout);
		material->m_IsDirty = true;

		// Add material to list
		if (std::find(m_ActiveMaterials.begin(), m_ActiveMaterials.end(), material) == m_ActiveMaterials.end()) {
			m_ActiveMaterials.push_back(material);
		}
	}

	void MaterialSystem::UnregisterMaterial(std::shared_ptr<Material> material) {
		auto it = std::find(m_ActiveMaterials.begin(), m_ActiveMaterials.end(), material);
		if (it != m_ActiveMaterials.end()) {
			m_ActiveMaterials.erase(it);
		}
	}

	void MaterialSystem::UpdateDirtyMaterials(nvrhi::ICommandList* commandList) {
		for (const auto& material : m_ActiveMaterials) {
			if(material->IsDirty() && material->m_ConstantBuffer) {
				commandList->writeBuffer(material->m_ConstantBuffer, &material->m_Constants, sizeof(MaterialConst));
			}

			material->ClearDirtyFlag();
		}
	}

	nvrhi::BindingLayoutHandle MaterialSystem::GetStandardMaterialLayout() const {
		return m_StandardLayout;
	}
}
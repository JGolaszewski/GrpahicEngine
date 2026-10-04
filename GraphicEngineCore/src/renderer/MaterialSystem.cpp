#include "renderer/MaterialSystem.h"

#include <algorithm>

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

	bool MaterialSystem::RegisterMaterial(std::shared_ptr<Material> material) {
		if (!material || std::find(m_ActiveMaterials.begin(), m_ActiveMaterials.end(), material) != m_ActiveMaterials.end()) {
			return false;
		}
		if (!m_Device || !m_StandardLayout) {
			return false;
		}

		// Allocate constant buffer for the material
		nvrhi::BufferDesc bufferDesc;
		bufferDesc.byteSize = sizeof(MaterialGpu);
		bufferDesc.isConstantBuffer = true;
		bufferDesc.isVolatile = false;
		bufferDesc.debugName = "Material_ConstantBuffer";
		material->m_ConstantBuffer = m_Device->createBuffer(bufferDesc);
		if (!material->m_ConstantBuffer) {
			return false;
		}

		// Create binding set for the material
		nvrhi::BindingSetDesc setDesc;
		setDesc.bindings = {
				nvrhi::BindingSetItem::ConstantBuffer(1, material->m_ConstantBuffer)
		};
		material->m_BindingSet = m_Device->createBindingSet(setDesc, m_StandardLayout);
		if (!material->m_BindingSet) {
			material->m_ConstantBuffer = nullptr;
			return false;
		}
		material->m_IsDirty = true;

		m_ActiveMaterials.push_back(std::move(material));
		return true;
	}

	void MaterialSystem::UnregisterMaterial(std::shared_ptr<Material> material) {
		auto it = std::find(m_ActiveMaterials.begin(), m_ActiveMaterials.end(), material);
		if (it != m_ActiveMaterials.end()) {
			m_ActiveMaterials.erase(it);
		}
	}

	void MaterialSystem::UpdateDirtyMaterials(nvrhi::ICommandList* commandList) {
		if (!commandList) {
			return;
		}

		for (const auto& material : m_ActiveMaterials) {
			if(material->IsDirty() && material->m_ConstantBuffer) {
				commandList->writeBuffer(material->m_ConstantBuffer, &material->m_Constants, sizeof(MaterialGpu));
				material->ClearDirtyFlag();
			}
		}
	}

	std::shared_ptr<Material> MaterialSystem::CreateMaterial(const MaterialProperties& properties) {
		auto material = std::make_shared<Material>(m_NextMaterialID++);
		material->SetProperties(properties);
		return RegisterMaterial(material) ? material : nullptr;
	}

	nvrhi::BindingLayoutHandle MaterialSystem::GetStandardMaterialLayout() const {
		return m_StandardLayout;
	}
}

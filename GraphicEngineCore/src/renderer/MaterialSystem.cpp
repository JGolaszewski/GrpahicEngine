#include "renderer/MaterialSystem.h"

namespace Renderer {
	MaterialSystem::MaterialSystem(nvrhi::IDevice* device)
		: m_Device(device) {
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

	void MaterialSystem::RegisterMaterial(Material* material) {
		if (std::find(m_ActiveMaterials.begin(), m_ActiveMaterials.end(), material) == m_ActiveMaterials.end()) {
			m_ActiveMaterials.push_back(material);
		}
	}

	void MaterialSystem::UnregisterMaterial(Material* material) {
		auto it = std::find(m_ActiveMaterials.begin(), m_ActiveMaterials.end(), material);
		if (it != m_ActiveMaterials.end()) {
			m_ActiveMaterials.erase(it);
		}
	}

	void MaterialSystem::UpdateDirtyMaterials() {
		for (auto material : m_ActiveMaterials) {
			material->UploadConstants(nullptr);
		}
	}

	nvrhi::BindingLayoutHandle MaterialSystem::GetStandardMaterialLayout() const {
		return m_StandardLayout;
	}
}
#include <nvrhi/nvrhi.h>
#include "Material.h"

namespace Renderer {
	class ENGINE_API MaterialSystem {
		MaterialSystem(nvrhi::IDevice* device);
		~MaterialSystem();

		void RegisterMaterial(Material* material);
		void UnregisterMaterial(Material* material);

		void UpdateDirtyMaterials();
		nvrhi::BindingLayoutHandle GetStandardMaterialLayout() const;
	private:
		nvrhi::DeviceHandle m_Device;
		nvrhi::BindingLayoutHandle m_StandardLayout;

		std::vector<Material*> m_ActiveMaterials;
	};

}
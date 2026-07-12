#include <nvrhi/nvrhi.h>
#include "Material.h"

#include <memory>

namespace Renderer {
	class ENGINE_API MaterialSystem {
	public:
		MaterialSystem(nvrhi::IDevice* device, nvrhi::ShaderHandle standardVS, nvrhi::ShaderHandle standardPS);
		~MaterialSystem();

		void RegisterMaterial(std::shared_ptr<Material> material);
		void UnregisterMaterial(std::shared_ptr<Material> material);

		void UpdateDirtyMaterials(nvrhi::ICommandList* commandList);
		nvrhi::BindingLayoutHandle GetStandardMaterialLayout() const;
		
		inline nvrhi::ShaderHandle GetStandardVS() const { return m_StandardVS; }
		inline nvrhi::ShaderHandle GetStandardPS() const { return m_StandardPS; }
	private:
		nvrhi::DeviceHandle m_Device;
		nvrhi::BindingLayoutHandle m_StandardLayout;

		nvrhi::ShaderHandle m_StandardVS;
		nvrhi::ShaderHandle m_StandardPS;

		std::vector<std::shared_ptr<Material>> m_ActiveMaterials;
	};

}
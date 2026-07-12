#pragma once

#include "CoreExport.h"
#include <nvrhi/nvrhi.h>
#include <DirectXMath.h>
#include <vector>

namespace Renderer {

	struct MaterialConst {
		DirectX::XMFLOAT4 BaseColor;
        DirectX::XMFLOAT4 EmissiveMetallic;
		DirectX::XMFLOAT4 SurfaceParams;
	};

	struct MaterialProperties {
		DirectX::XMFLOAT4 BaseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		DirectX::XMFLOAT3 Emissive = { 0.0f, 0.0f, 0.0f };
		float Metallic = 0.0f;
		float Roughness = 0.5f;
	};

	class ENGINE_API Material {
		friend class MaterialSystem;
	
	public:
		Material(uint32_t materialID);

        void SetProperties(const MaterialProperties& properties);

		void SetColor(const DirectX::XMFLOAT4& color);	
		void SetEmissive(const DirectX::XMFLOAT3& emissive);
		void SetMetallic(float metallic);
		void SetRoughness(float roughness);

		inline void ClearDirtyFlag() { m_IsDirty = false; }
		inline bool IsDirty() const { return m_IsDirty; }

		MaterialProperties GetProperties() const;
		inline nvrhi::BindingSetHandle GetBindingSet() const { return m_BindingSet; }
	private:
		bool m_IsDirty = true;
		uint32_t m_MaterialID;

		nvrhi::BufferHandle m_ConstantBuffer;
		nvrhi::BindingSetHandle m_BindingSet;

		MaterialConst m_Constants;
	};
}
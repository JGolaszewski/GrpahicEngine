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
	public:
		Material(nvrhi::IDevice* device, nvrhi::ShaderHandle vs, nvrhi::ShaderHandle ps);

        void SetProperties(const MaterialProperties& properties, nvrhi::ICommandList* commandList);
		void SetColor(const DirectX::XMFLOAT4& color, nvrhi::ICommandList* commandList);
		void SetEmissive(const DirectX::XMFLOAT3& emissive, nvrhi::ICommandList* commandList);
		void SetMetallic(float metallic, nvrhi::ICommandList* commandList);
		void SetRoughness(float roughness, nvrhi::ICommandList* commandList);
		MaterialProperties GetProperties() const;

		inline nvrhi::BindingSetHandle GetBindingSet() const { return m_BindingSet; }
		inline nvrhi::GraphicsPipelineHandle GetPipeline() const { return m_Pipeline; }
		inline nvrhi::BindingLayoutHandle GetBindingLayout() const { return m_BindingLayout; }

		inline nvrhi::ShaderHandle GetVS() const { return m_VS; }
		inline nvrhi::ShaderHandle GetPS() const { return m_PS; }

		static const Material& DefaultMaterial(nvrhi::IDevice* device);
	private:
		bool m_IsDirty = true;
		uint32_t m_MaterialID;

		nvrhi::DeviceHandle m_Device;

		nvrhi::ShaderHandle m_VS;
		nvrhi::ShaderHandle m_PS;

		nvrhi::BufferHandle m_ConstantBuffer;
		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::BindingSetHandle m_BindingSet;

		nvrhi::InputLayoutHandle m_InputLayout;
		nvrhi::GraphicsPipelineHandle m_Pipeline;

		MaterialConst m_Constants = {
			{ 1.0f, 1.0f, 1.0f, 1.0f },
			{ 0.0f, 0.0f, 0.0f, 0.0f },
			{ 0.5f, 0.0f, 0.0f, 0.0f }
		};

		void UploadConstants(nvrhi::ICommandList* commandList);
	};
}
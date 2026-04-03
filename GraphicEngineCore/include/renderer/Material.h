#pragma once

#include "CoreExport.h"
#include <nvrhi/nvrhi.h>
#include <DirectXMath.h>
#include <vector>

namespace Renderer {

	struct MaterialConst {
		DirectX::XMFLOAT4 BaseColor;
	};

	class ENGINE_API Material {
	public:
		Material(nvrhi::IDevice* device, nvrhi::ShaderHandle vs, nvrhi::ShaderHandle ps);

		void SetColor(const DirectX::XMFLOAT4& color, nvrhi::ICommandList* commandList);

		inline nvrhi::BindingSetHandle GetBindingSet() const { return m_BindingSet; }
		inline nvrhi::GraphicsPipelineHandle GetPipeline() const { return m_Pipeline; }
		inline nvrhi::BindingLayoutHandle GetBindingLayout() const { return m_BindingLayout; }

		inline nvrhi::ShaderHandle GetVS() const { return m_VS; }
		inline nvrhi::ShaderHandle GetPS() const { return m_PS; }
	private:
		nvrhi::DeviceHandle m_Device;

		nvrhi::ShaderHandle m_VS;
		nvrhi::ShaderHandle m_PS;

		nvrhi::BufferHandle m_ConstantBuffer;
		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::BindingSetHandle m_BindingSet;

		nvrhi::InputLayoutHandle m_InputLayout;
		nvrhi::GraphicsPipelineHandle m_Pipeline;
	};
}
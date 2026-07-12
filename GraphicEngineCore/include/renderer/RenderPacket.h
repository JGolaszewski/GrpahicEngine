#pragma once

#include <nvrhi.h>
#include <DirectXMath.h>

namespace Renderer {
	struct RenderPacket {
		DirectX::XMFLOAT4X4 WorldMatrix;
		nvrhi::BufferHandle VertexBuffer;
		nvrhi::BufferHandle IndexBuffer;
		nvrhi::BindingSetHandle MaterialBindings;
		nvrhi::GraphicsPipelineHandle Pipeline;
		uint32_t IndexCount;
	};
}
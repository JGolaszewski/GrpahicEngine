#pragma once

#include "CoreExport.h"

#include <DirectXMath.h>
#include <nvrhi/nvrhi.h>
#include <stdint.h>
#include <vector>

namespace Renderer {
	struct ENGINE_API Vertex {
		DirectX::XMFLOAT3 Position;
		DirectX::XMFLOAT3 Normal;
		DirectX::XMFLOAT2 UV;
		DirectX::XMFLOAT4 Tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
	};
	 
	class ENGINE_API Mesh {
	public:
		Mesh(nvrhi::IDevice* device, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices = {});

		void UploadData(nvrhi::ICommandList* commandList, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices = {});

		static std::vector<nvrhi::VertexAttributeDesc> GetVertexAttributes() {
			return {
				nvrhi::VertexAttributeDesc()
					.setName("POSITION")
					.setFormat(nvrhi::Format::RGB32_FLOAT)
					.setOffset(offsetof(Vertex, Position))
					.setElementStride(sizeof(Vertex)),
				nvrhi::VertexAttributeDesc()
					.setName("NORMAL")
					.setFormat(nvrhi::Format::RGB32_FLOAT)
					.setOffset(offsetof(Vertex, Normal))
					.setElementStride(sizeof(Vertex)),
				nvrhi::VertexAttributeDesc()
					.setName("TEXCOORD")
					.setFormat(nvrhi::Format::RG32_FLOAT)
					.setOffset(offsetof(Vertex, UV))
					.setElementStride(sizeof(Vertex)),
				nvrhi::VertexAttributeDesc()
					.setName("TANGENT")
					.setFormat(nvrhi::Format::RGBA32_FLOAT)
					.setOffset(offsetof(Vertex, Tangent))
					.setElementStride(sizeof(Vertex))
			};
		}

		inline nvrhi::BufferHandle GetVertexBuffer() const { return m_VertexBuffer; }
		inline nvrhi::BufferHandle GetIndexBuffer() const { return m_IndexBuffer; }
		inline uint32_t GetVertexCount() const { return m_VertexCount; }
		inline uint32_t GetIndexCount() const { return m_IndexCount; }
		inline bool HasIndices() const { return m_IndexBuffer != nullptr; }
		inline bool IsValid() const { return m_VertexBuffer != nullptr && (m_IndexCount == 0 || m_IndexBuffer != nullptr); }

	private:
		nvrhi::BufferHandle m_VertexBuffer;
		nvrhi::BufferHandle m_IndexBuffer;

		uint32_t m_VertexCount = 0;
		uint32_t m_IndexCount = 0;
	};
}

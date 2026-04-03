#pragma once

#include "CoreExport.h"

#include <DirectXMath.h>
#include <nvrhi/nvrhi.h>
#include <stdint.h>
#include <vector>

namespace Renderer {
	// USUNIÊTO: class Material; (Mesh ju¿ o nim nie wie!)

	struct ENGINE_API Vertex {
		DirectX::XMFLOAT3 Position;
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
					.setElementStride(sizeof(Vertex))
			};
		}

		inline nvrhi::BufferHandle GetVertexBuffer() const { return m_VertexBuffer; }
		inline nvrhi::BufferHandle GetIndexBuffer() const { return m_IndexBuffer; }
		inline uint32_t GetVertexCount() const { return m_VertexCount; }
		inline uint32_t GetIndexCount() const { return m_IndexCount; }
		inline bool HasIndices() const { return m_IndexBuffer != nullptr; }

	private:
		nvrhi::BufferHandle m_VertexBuffer;
		nvrhi::BufferHandle m_IndexBuffer;

		uint32_t m_VertexCount = 0;
		uint32_t m_IndexCount = 0;
	};
}
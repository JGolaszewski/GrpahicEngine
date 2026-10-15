#include "rendering/include/renderer/Mesh.h"

#include <algorithm>

namespace Engine::Renderer {

Mesh::Mesh(nvrhi::IDevice* device, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
           nvrhi::PrimitiveType primitiveType)
    : m_VertexCount(static_cast<uint32_t>(vertices.size())), m_IndexCount(static_cast<uint32_t>(indices.size())),
      m_PrimitiveType(primitiveType) {

  if (m_VertexCount > 0) {
    nvrhi::BufferDesc vbDesc;
    vbDesc.byteSize = m_VertexCount * sizeof(Vertex);
    vbDesc.isVertexBuffer = true;
    vbDesc.debugName = "VertexBuffer";
    m_VertexBuffer = device->createBuffer(vbDesc);
  }

  if (m_IndexCount > 0) {
    nvrhi::BufferDesc ibDesc;
    ibDesc.byteSize = m_IndexCount * sizeof(uint32_t);
    ibDesc.isIndexBuffer = true;
    ibDesc.debugName = "IndexBuffer";
    m_IndexBuffer = device->createBuffer(ibDesc);
  }
}

void Mesh::UploadData(nvrhi::ICommandList* commandList, const std::vector<Vertex>& vertices,
                      const std::vector<uint32_t>& indices) {
  if ((m_VertexBuffer != nullptr) && !vertices.empty()) {
    size_t bytesToCopy = std::min(vertices.size(), static_cast<size_t>(m_VertexCount)) * sizeof(Vertex);
    commandList->writeBuffer(m_VertexBuffer, vertices.data(), bytesToCopy);
  }

  if ((m_IndexBuffer != nullptr) && !indices.empty()) {
    size_t bytesToCopy = std::min(indices.size(), static_cast<size_t>(m_IndexCount)) * sizeof(uint32_t);
    commandList->writeBuffer(m_IndexBuffer, indices.data(), bytesToCopy);
  }
}

} // namespace Engine::Renderer

#include "rendering/include/renderer/Mesh.h"

#include "MeshBackend.h"

#include <algorithm>
#include <cstddef>

namespace Engine::Renderer {
struct Mesh::Impl {
  nvrhi::BufferHandle VertexBuffer;
  nvrhi::BufferHandle IndexBuffer;
  uint32_t VertexCount = 0;
  uint32_t IndexCount = 0;
  PrimitiveTopology Topology = PrimitiveTopology::Triangles;
};

std::shared_ptr<Mesh> MeshBackendAccess::Create(nvrhi::IDevice* device, const std::vector<Vertex>& vertices,
                                                const std::vector<uint32_t>& indices, PrimitiveTopology topology) {
  auto mesh = std::shared_ptr<Mesh>(new Mesh());
  return Initialize(*mesh, device, vertices, indices, topology) ? mesh : nullptr;
}

Mesh::Mesh() : m_Impl(std::make_unique<Impl>()) {}

Mesh::~Mesh() = default;

uint32_t Mesh::GetVertexCount() const noexcept { return m_Impl->VertexCount; }

uint32_t Mesh::GetIndexCount() const noexcept { return m_Impl->IndexCount; }

bool Mesh::HasIndices() const noexcept { return m_Impl->IndexBuffer != nullptr; }

bool Mesh::IsValid() const noexcept {
  return m_Impl->VertexBuffer != nullptr && (m_Impl->IndexCount == 0 || m_Impl->IndexBuffer != nullptr);
}

PrimitiveTopology Mesh::GetPrimitiveTopology() const noexcept { return m_Impl->Topology; }

bool MeshBackendAccess::Initialize(Mesh& mesh, nvrhi::IDevice* device, const std::vector<Vertex>& vertices,
                                   const std::vector<uint32_t>& indices, PrimitiveTopology topology) {
  auto& impl = *mesh.m_Impl;
  impl.VertexCount = static_cast<uint32_t>(vertices.size());
  impl.IndexCount = static_cast<uint32_t>(indices.size());
  impl.Topology = topology;

  if (impl.VertexCount > 0) {
    nvrhi::BufferDesc vertexBufferDesc;
    vertexBufferDesc.byteSize = impl.VertexCount * sizeof(Vertex);
    vertexBufferDesc.isVertexBuffer = true;
    vertexBufferDesc.debugName = "VertexBuffer";
    impl.VertexBuffer = device->createBuffer(vertexBufferDesc);
  }

  if (impl.IndexCount > 0) {
    nvrhi::BufferDesc indexBufferDesc;
    indexBufferDesc.byteSize = impl.IndexCount * sizeof(uint32_t);
    indexBufferDesc.isIndexBuffer = true;
    indexBufferDesc.debugName = "IndexBuffer";
    impl.IndexBuffer = device->createBuffer(indexBufferDesc);
  }

  return mesh.IsValid();
}

void MeshBackendAccess::UploadData(Mesh& mesh, nvrhi::ICommandList* commandList, const std::vector<Vertex>& vertices,
                                   const std::vector<uint32_t>& indices) {
  auto& impl = *mesh.m_Impl;
  if (impl.VertexBuffer && !vertices.empty()) {
    const size_t bytesToCopy = std::min(vertices.size(), static_cast<size_t>(impl.VertexCount)) * sizeof(Vertex);
    commandList->writeBuffer(impl.VertexBuffer, vertices.data(), bytesToCopy);
  }

  if (impl.IndexBuffer && !indices.empty()) {
    const size_t bytesToCopy = std::min(indices.size(), static_cast<size_t>(impl.IndexCount)) * sizeof(uint32_t);
    commandList->writeBuffer(impl.IndexBuffer, indices.data(), bytesToCopy);
  }
}

nvrhi::BufferHandle MeshBackendAccess::GetVertexBuffer(const Mesh& mesh) { return mesh.m_Impl->VertexBuffer; }

nvrhi::BufferHandle MeshBackendAccess::GetIndexBuffer(const Mesh& mesh) { return mesh.m_Impl->IndexBuffer; }

std::vector<nvrhi::VertexAttributeDesc> MeshBackendAccess::GetVertexAttributes() {
  return {nvrhi::VertexAttributeDesc()
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
              .setArraySize(MaxTextureCoordinateSets)
              .setFormat(nvrhi::Format::RG32_FLOAT)
              .setOffset(offsetof(Vertex, UV))
              .setElementStride(sizeof(Vertex)),
          nvrhi::VertexAttributeDesc()
              .setName("TANGENT")
              .setFormat(nvrhi::Format::RGBA32_FLOAT)
              .setOffset(offsetof(Vertex, Tangent))
              .setElementStride(sizeof(Vertex)),
          nvrhi::VertexAttributeDesc()
              .setName("COLOR")
              .setFormat(nvrhi::Format::RGBA32_FLOAT)
              .setOffset(offsetof(Vertex, Color))
              .setElementStride(sizeof(Vertex))};
}

static_assert(offsetof(Vertex, Tangent) == offsetof(Vertex, UV) + sizeof(DirectX::XMFLOAT2) * MaxTextureCoordinateSets);
} // namespace Engine::Renderer

#pragma once

#include "CoreExport.h"
#include <DirectXMath.h>
#include <nvrhi/nvrhi.h>
#include <cstddef>
#include <stdint.h>
#include <vector>

namespace Renderer
{
struct ENGINE_API Vertex
{
    Vertex() = default;
    Vertex(const DirectX::XMFLOAT3 &position, const DirectX::XMFLOAT3 &normal,
           const DirectX::XMFLOAT2 &uv)
        : Position(position), Normal(normal)
    {
        UV[0] = uv;
    }

    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 UV[MaxTextureCoordinateSets] = {};
    DirectX::XMFLOAT4 Tangent = {1.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4 Color = {1.0f, 1.0f, 1.0f, 1.0f};
};

class ENGINE_API Mesh
{
  public:
    Mesh(nvrhi::IDevice *device, const std::vector<Vertex> &vertices,
         const std::vector<uint32_t> &indices = {},
         nvrhi::PrimitiveType primitiveType = nvrhi::PrimitiveType::TriangleList);

    void UploadData(nvrhi::ICommandList *commandList, const std::vector<Vertex> &vertices,
                    const std::vector<uint32_t> &indices = {});

    static std::vector<nvrhi::VertexAttributeDesc> GetVertexAttributes()
    {
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

    inline nvrhi::BufferHandle GetVertexBuffer() const
    {
        return m_VertexBuffer;
    }
    inline nvrhi::BufferHandle GetIndexBuffer() const
    {
        return m_IndexBuffer;
    }
    inline uint32_t GetVertexCount() const
    {
        return m_VertexCount;
    }
    inline uint32_t GetIndexCount() const
    {
        return m_IndexCount;
    }
    inline bool HasIndices() const
    {
        return m_IndexBuffer != nullptr;
    }
    inline bool IsValid() const
    {
        return m_VertexBuffer != nullptr && (m_IndexCount == 0 || m_IndexBuffer != nullptr);
    }
    nvrhi::PrimitiveType GetPrimitiveType() const noexcept
    {
        return m_PrimitiveType;
    }

  private:
    nvrhi::BufferHandle m_VertexBuffer;
    nvrhi::BufferHandle m_IndexBuffer;

    uint32_t m_VertexCount = 0;
    uint32_t m_IndexCount = 0;
    nvrhi::PrimitiveType m_PrimitiveType = nvrhi::PrimitiveType::TriangleList;
};

static_assert(offsetof(Vertex, Tangent) == offsetof(Vertex, UV) +
              sizeof(DirectX::XMFLOAT2) * MaxTextureCoordinateSets);
} // namespace Renderer

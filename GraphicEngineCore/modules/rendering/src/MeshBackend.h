#pragma once

#include "rendering/include/renderer/Mesh.h"

#include <nvrhi/nvrhi.h>

#include <vector>

namespace Engine::Renderer
{
class MeshBackendAccess
{
  public:
    static bool Initialize(Mesh &mesh, nvrhi::IDevice *device, const std::vector<Vertex> &vertices,
                           const std::vector<uint32_t> &indices, PrimitiveTopology topology);
    static void UploadData(Mesh &mesh, nvrhi::ICommandList *commandList, const std::vector<Vertex> &vertices,
                           const std::vector<uint32_t> &indices);
    static nvrhi::BufferHandle GetVertexBuffer(const Mesh &mesh);
    static nvrhi::BufferHandle GetIndexBuffer(const Mesh &mesh);
    static std::vector<nvrhi::VertexAttributeDesc> GetVertexAttributes();
};
} // namespace Engine::Renderer

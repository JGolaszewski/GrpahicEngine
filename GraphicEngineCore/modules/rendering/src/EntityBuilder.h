#pragma once

#include "rendering/include/renderer/Entity.h"
#include "rendering/include/renderer/Material.h"
#include "rendering/include/renderer/Mesh.h"
#include "rendering/include/renderer/Vertex.h"

#include <nvrhi/nvrhi.h>

namespace Engine::Renderer
{
class EntityBuilder
{
  public:
    EntityBuilder(nvrhi::DeviceHandle device, nvrhi::CommandListHandle commandList);

    EntityBuilder &Cube();
    EntityBuilder &Cone(uint32_t segments = 24);
    EntityBuilder &Sphere(uint32_t stacks = 16, uint32_t slices = 24);
    EntityBuilder &Plane();
    EntityBuilder &SetMaterial(std::shared_ptr<Material> material);
    EntityBuilder &Scale(float x, float y, float z);
    EntityBuilder &Position(float x, float y, float z);
    std::shared_ptr<Entity> Build();

  private:
    nvrhi::DeviceHandle m_Device;
    nvrhi::CommandListHandle m_CommandList;

    enum class Shape
    {
        None,
        Cube,
        Cone,
        Sphere,
        Plane
    };
    Shape m_Shape = Shape::None;
    uint32_t m_ConeSegments = 24;
    uint32_t m_SphereStacks = 16;
    uint32_t m_SphereSlices = 24;
    std::shared_ptr<Material> m_Material;
    DirectX::XMFLOAT3 m_Position = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 m_Scale = {1.0f, 1.0f, 1.0f};

    void GenerateCubeData(std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices);
    void GenerateConeData(std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices,
                          uint32_t segments);
    void GenerateSphereData(std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices,
                            uint32_t stacks, uint32_t slices);
    void GeneratePlaneData(std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices);
};
} // namespace Engine::Renderer

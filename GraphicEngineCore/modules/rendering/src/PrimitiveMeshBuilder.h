#pragma once

#include "rendering/include/renderer/RenderResources.h"

#include <optional>

namespace Engine::Renderer {
class PrimitiveMeshBuilder {
public:
  PrimitiveMeshBuilder(nvrhi::DeviceHandle device, nvrhi::CommandListHandle commandList);

  PrimitiveMeshBuilder& Cube();
  PrimitiveMeshBuilder& Cone(uint32_t segments = 24);
  PrimitiveMeshBuilder& Sphere(uint32_t stacks = 16, uint32_t slices = 24);
  PrimitiveMeshBuilder& Plane();
  PrimitiveMeshBuilder& SetMaterial(std::shared_ptr<Material> material);
  std::optional<Submesh> Build();

private:
  nvrhi::DeviceHandle m_Device;
  nvrhi::CommandListHandle m_CommandList;

  enum class Shape { None, Cube, Cone, Sphere, Plane };
  Shape m_Shape = Shape::None;
  uint32_t m_ConeSegments = 24;
  uint32_t m_SphereStacks = 16;
  uint32_t m_SphereSlices = 24;
  std::shared_ptr<Material> m_Material;

  static void GenerateCubeData(std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices);
  static void GenerateConeData(std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices, uint32_t segments);
  static void GenerateSphereData(std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices, uint32_t stacks,
                                 uint32_t slices);
  static void GeneratePlaneData(std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices);
};
} // namespace Engine::Renderer

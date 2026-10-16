#include "rendering/src/PrimitiveMeshBuilder.h"
#include "MeshBackend.h"
#include "PrimitiveMeshGeometry.h"
#include "rendering/include/renderer/RenderResources.h"
#include "runtime/include/LogMacros.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace Engine::Renderer {

PrimitiveMeshBuilder::PrimitiveMeshBuilder(nvrhi::DeviceHandle device, nvrhi::CommandListHandle commandList)
    : m_Device(std::move(device)), m_CommandList(std::move(commandList)) {}

auto PrimitiveMeshBuilder::Cube() -> ::Engine::Renderer::PrimitiveMeshBuilder& {
  m_Shape = Shape::Cube;
  return *this;
}

auto PrimitiveMeshBuilder::Cone(uint32_t segments) -> ::Engine::Renderer::PrimitiveMeshBuilder& {
  m_Shape = Shape::Cone;
  m_ConeSegments = std::max<uint32_t>(3U, segments);
  return *this;
}

auto PrimitiveMeshBuilder::Sphere(uint32_t stacks, uint32_t slices) -> ::Engine::Renderer::PrimitiveMeshBuilder& {
  m_Shape = Shape::Sphere;
  m_SphereStacks = std::max<uint32_t>(2U, stacks);
  m_SphereSlices = std::max<uint32_t>(3U, slices);
  return *this;
}

auto PrimitiveMeshBuilder::Plane() -> ::Engine::Renderer::PrimitiveMeshBuilder& {
  m_Shape = Shape::Plane;
  return *this;
}

auto PrimitiveMeshBuilder::SetMaterial(std::shared_ptr<::Engine::Renderer::Material> material)
    -> ::Engine::Renderer::PrimitiveMeshBuilder& {
  m_Material = std::move(material);
  return *this;
}

auto PrimitiveMeshBuilder::Build() -> std::optional<::Engine::Renderer::Submesh> {
  if ((m_Device == nullptr) || (m_CommandList == nullptr) || !m_Material) {
    ENGINE_LOG_ERROR(
        "ECS", "Primitive mesh creation rejected | device_valid: {} | command_list_valid: {} | material_valid: {}",
        m_Device != nullptr, m_CommandList != nullptr, m_Material != nullptr);
    return std::nullopt;
  }

  std::vector<::Engine::Renderer::Vertex> vertices;
  std::vector<uint32_t> indices;

  if (m_Shape == Shape::Cube) {
    GenerateCubeData(vertices, indices);
  } else if (m_Shape == Shape::Cone) {
    PrimitiveMeshGeometry::GenerateConeData(vertices, indices, m_ConeSegments);
  } else if (m_Shape == Shape::Sphere) {
    GenerateSphereData(vertices, indices, m_SphereStacks, m_SphereSlices);
  } else if (m_Shape == Shape::Plane) {
    GeneratePlaneData(vertices, indices);
  } else {
    ENGINE_LOG_ERROR("ECS", "Primitive mesh creation rejected | shape: None | material_valid: {}",
                     m_Material != nullptr);
    return std::nullopt;
  }

  auto mesh = MeshBackendAccess::Create(m_Device, vertices, indices, PrimitiveTopology::Triangles);
  if (!mesh) {
    ENGINE_LOG_ERROR("ECS", "Primitive mesh creation failed | vertices: {} | indices: {}", vertices.size(),
                     indices.size());
    return std::nullopt;
  }
  MeshBackendAccess::UploadData(*mesh, m_CommandList, vertices, indices);

  // Scene transforms and identity are represented by ECS components.
  ::Engine::Renderer::Submesh submesh;
  submesh.mesh = std::move(mesh);
  submesh.material = m_Material;
  submesh.BoundsMin = {-1.0F, -1.0F, -1.0F};
  submesh.BoundsMax = {1.0F, 1.0F, 1.0F};
  return submesh;
}

void PrimitiveMeshBuilder::GenerateCubeData(std::vector<::Engine::Renderer::Vertex>& outVertices,
                                            std::vector<uint32_t>& outIndices) {
  // Zaktualizowano o poprawne współrzędne UV na każdą ścianę
  outVertices = {
      // Front
      {{-0.5F, 0.5F, -0.5F}, {0.0F, 0.0F, -1.0F}, {0.0F, 0.0F}},
      {{0.5F, 0.5F, -0.5F}, {0.0F, 0.0F, -1.0F}, {1.0F, 0.0F}},
      {{0.5F, -0.5F, -0.5F}, {0.0F, 0.0F, -1.0F}, {1.0F, 1.0F}},
      {{-0.5F, -0.5F, -0.5F}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F}},
      // Back
      {{0.5F, 0.5F, 0.5F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F}},
      {{-0.5F, 0.5F, 0.5F}, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F}},
      {{-0.5F, -0.5F, 0.5F}, {0.0F, 0.0F, 1.0F}, {1.0F, 1.0F}},
      {{0.5F, -0.5F, 0.5F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F}},
      // Top
      {{-0.5F, 0.5F, 0.5F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F}},
      {{0.5F, 0.5F, 0.5F}, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F}},
      {{0.5F, 0.5F, -0.5F}, {0.0F, 1.0F, 0.0F}, {1.0F, 1.0F}},
      {{-0.5F, 0.5F, -0.5F}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F}},
      // Bottom
      {{-0.5F, -0.5F, -0.5F}, {0.0F, -1.0F, 0.0F}, {0.0F, 0.0F}},
      {{0.5F, -0.5F, -0.5F}, {0.0F, -1.0F, 0.0F}, {1.0F, 0.0F}},
      {{0.5F, -0.5F, 0.5F}, {0.0F, -1.0F, 0.0F}, {1.0F, 1.0F}},
      {{-0.5F, -0.5F, 0.5F}, {0.0F, -1.0F, 0.0F}, {0.0F, 1.0F}},
      // Right
      {{0.5F, 0.5F, -0.5F}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F}},
      {{0.5F, 0.5F, 0.5F}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F}},
      {{0.5F, -0.5F, 0.5F}, {1.0F, 0.0F, 0.0F}, {1.0F, 1.0F}},
      {{0.5F, -0.5F, -0.5F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F}},
      // Left
      {{-0.5F, 0.5F, 0.5F}, {-1.0F, 0.0F, 0.0F}, {0.0F, 0.0F}},
      {{-0.5F, 0.5F, -0.5F}, {-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F}},
      {{-0.5F, -0.5F, -0.5F}, {-1.0F, 0.0F, 0.0F}, {1.0F, 1.0F}},
      {{-0.5F, -0.5F, 0.5F}, {-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F}},
  };

  for (int i = 0; i < 6; ++i) {
    uint32_t offset = i * 4;
    outIndices.push_back(offset + 0);
    outIndices.push_back(offset + 1);
    outIndices.push_back(offset + 2);
    outIndices.push_back(offset + 0);
    outIndices.push_back(offset + 2);
    outIndices.push_back(offset + 3);
  }
}

void PrimitiveMeshBuilder::GenerateSphereData(std::vector<::Engine::Renderer::Vertex>& outVertices,
                                              std::vector<uint32_t>& outIndices, uint32_t stacks, uint32_t slices) {
  outVertices.clear();
  outIndices.clear();

  constexpr float pi = std::numbers::pi_v<float>;
  constexpr float twoPi = 6.2831853071795864769F;

  for (uint32_t stack = 0; stack <= stacks; ++stack) {
    const float v = static_cast<float>(stack) / static_cast<float>(stacks);
    const float phi = v * pi;

    const float y = std::cos(phi);
    const float r = std::sin(phi);

    for (uint32_t slice = 0; slice <= slices; ++slice) {
      const float u = static_cast<float>(slice) / static_cast<float>(slices);
      const float theta = u * twoPi;

      const float x = r * std::cos(theta);
      const float z = r * std::sin(theta);

      // UV Sphere Map
      outVertices.push_back({{x, y, z}, {x, y, z}, {u, v}});
    }
  }

  const uint32_t ringVertexCount = slices + 1;
  for (uint32_t stack = 0; stack < stacks; ++stack) {
    for (uint32_t slice = 0; slice < slices; ++slice) {
      const uint32_t i0 = (stack * ringVertexCount) + slice;
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = i0 + ringVertexCount;
      const uint32_t i3 = i2 + 1;

      outIndices.push_back(i0);
      outIndices.push_back(i1);
      outIndices.push_back(i2);

      outIndices.push_back(i1);
      outIndices.push_back(i3);
      outIndices.push_back(i2);
    }
  }
}

void PrimitiveMeshBuilder::GeneratePlaneData(std::vector<::Engine::Renderer::Vertex>& outVertices,
                                             std::vector<uint32_t>& outIndices) {
  outVertices = {
      {{-0.5F, 0.0F, -0.5F}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F}},
      {{0.5F, 0.0F, -0.5F}, {0.0F, 1.0F, 0.0F}, {1.0F, 1.0F}},
      {{0.5F, 0.0F, 0.5F}, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F}},
      {{-0.5F, 0.0F, 0.5F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F}},
  };

  outIndices = {0, 2, 1, 0, 3, 2};
}

} // namespace Engine::Renderer

#include "PrimitiveMeshGeometry.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace {

auto Dot(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs) -> float {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

auto Subtract(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs) -> DirectX::XMFLOAT3 {
  return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

auto Cross(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs) -> DirectX::XMFLOAT3 {
  return {lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z, lhs.x * rhs.y - lhs.y * rhs.x};
}

auto TriangleNormal(const Engine::Renderer::Vertex& a, const Engine::Renderer::Vertex& b,
                    const Engine::Renderer::Vertex& c) -> DirectX::XMFLOAT3 {
  return Cross(Subtract(b.Position, a.Position), Subtract(c.Position, a.Position));
}

} // namespace

TEST(PrimitiveConeGeometryTests, WindingAndVertexNormalsPointOutwardOnSidesAndBase) {
  constexpr uint32_t segments = 16;
  std::vector<Engine::Renderer::Vertex> vertices;
  std::vector<uint32_t> indices;
  Engine::Renderer::PrimitiveMeshGeometry::GenerateConeData(vertices, indices, segments);

  ASSERT_EQ(vertices.size(), static_cast<size_t>(segments) + 2U);
  ASSERT_EQ(indices.size(), static_cast<size_t>(segments) * 6U);

  for (const auto& vertex : vertices) {
    const float length = std::sqrt(Dot(vertex.Normal, vertex.Normal));
    EXPECT_NEAR(length, 1.0F, 1.0e-5F);
  }

  for (uint32_t segment = 0; segment < segments; ++segment) {
    const size_t sideIndex = static_cast<size_t>(segment) * 6U;
    const auto& apex = vertices[indices[sideIndex]];
    const auto& sideA = vertices[indices[sideIndex + 1U]];
    const auto& sideB = vertices[indices[sideIndex + 2U]];
    const DirectX::XMFLOAT3 sideFaceNormal = TriangleNormal(apex, sideA, sideB);
    const DirectX::XMFLOAT3 averageSideNormal = {(apex.Normal.x + sideA.Normal.x + sideB.Normal.x) / 3.0F,
                                                 (apex.Normal.y + sideA.Normal.y + sideB.Normal.y) / 3.0F,
                                                 (apex.Normal.z + sideA.Normal.z + sideB.Normal.z) / 3.0F};
    EXPECT_GT(Dot(sideFaceNormal, averageSideNormal), 0.0F) << "side segment " << segment;
    EXPECT_LT(sideFaceNormal.z, 0.0F) << "side segment " << segment;
    EXPECT_GT(sideFaceNormal.x * (sideA.Position.x + sideB.Position.x) +
                  sideFaceNormal.y * (sideA.Position.y + sideB.Position.y),
              0.0F)
        << "side segment " << segment;

    const auto& baseCenter = vertices[indices[sideIndex + 3U]];
    const auto& baseA = vertices[indices[sideIndex + 4U]];
    const auto& baseB = vertices[indices[sideIndex + 5U]];
    const DirectX::XMFLOAT3 baseFaceNormal = TriangleNormal(baseCenter, baseA, baseB);
    EXPECT_GT(baseFaceNormal.z, 0.0F) << "base segment " << segment;
    EXPECT_GT(Dot(baseFaceNormal, baseCenter.Normal), 0.0F) << "base segment " << segment;
  }
}

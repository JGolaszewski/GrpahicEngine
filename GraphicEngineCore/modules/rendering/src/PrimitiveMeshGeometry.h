#pragma once

#include "rendering/include/renderer/Vertex.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace Engine::Renderer::PrimitiveMeshGeometry {

/// Generates a unit cone with its apex at z=0 and base at z=1.
/// Triangle winding and vertex normals point outward from the side and base.
inline void GenerateConeData(std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices, uint32_t segments) {
  outVertices.clear();
  outIndices.clear();

  outVertices.push_back({{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {0.5F, 0.0F}});

  constexpr float twoPi = 6.2831853071795864769F;
  constexpr float inverseSqrtTwo = 0.7071067811865475244F;
  for (uint32_t i = 0; i < segments; ++i) {
    const float u = static_cast<float>(i) / static_cast<float>(segments);
    const float angle = u * twoPi;
    const float x = std::cos(angle);
    const float y = std::sin(angle);
    const DirectX::XMFLOAT3 normal(x * inverseSqrtTwo, y * inverseSqrtTwo, -inverseSqrtTwo);
    outVertices.push_back({{x, y, 1.0F}, normal, {u, 1.0F}});
  }

  outVertices.push_back({{0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F}, {0.5F, 0.5F}});
  const uint32_t baseCenter = static_cast<uint32_t>(outVertices.size() - 1);

  for (uint32_t i = 0; i < segments; ++i) {
    const uint32_t a = 1 + i;
    const uint32_t b = 1 + ((i + 1) % segments);

    // Reversing the ring order makes the side face normal point radially out
    // and toward the cone tip (negative z).
    outIndices.push_back(0);
    outIndices.push_back(b);
    outIndices.push_back(a);

    // The base faces toward positive z, away from the cone interior.
    outIndices.push_back(baseCenter);
    outIndices.push_back(a);
    outIndices.push_back(b);
  }
}

} // namespace Engine::Renderer::PrimitiveMeshGeometry

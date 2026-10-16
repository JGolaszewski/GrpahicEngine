#pragma once

#include "rendering/include/renderer/Material.h"
#include "rendering/include/renderer/Mesh.h"

#include <DirectXMath.h>
#include <cstdint>
#include <memory>

namespace Engine::Renderer {
/// Mesh/material pair and local bounds used as one imported or primitive submesh.
struct Submesh {
  std::shared_ptr<Mesh> mesh; ///< GPU mesh resource.
  std::shared_ptr<Material> material; ///< Material applied to the mesh.
  DirectX::XMFLOAT4X4 LocalTransform = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}; ///< Transform from mesh to model space.
  DirectX::XMFLOAT3 BoundsMin{}; ///< Minimum mesh-space bounds.
  DirectX::XMFLOAT3 BoundsMax{}; ///< Maximum mesh-space bounds.
  int32_t MaterialSlot = -1; ///< Source material slot, or -1 when not assigned.
  mutable bool WorldBoundsCached = false; ///< Whether world bounds match the cached transform.
  mutable DirectX::XMFLOAT4X4 CachedWorldTransform{}; ///< Transform used to produce cached world bounds.
  mutable DirectX::XMFLOAT3 CachedBoundsMin{}; ///< Cached world-space minimum bounds.
  mutable DirectX::XMFLOAT3 CachedBoundsMax{}; ///< Cached world-space maximum bounds.
  mutable DirectX::XMFLOAT3 CachedWorldBoundsCenter{}; ///< Cached world-space bounding-sphere center.
  mutable float CachedWorldBoundsRadius = 0.0f; ///< Cached world-space bounding-sphere radius.
};
} // namespace Engine::Renderer

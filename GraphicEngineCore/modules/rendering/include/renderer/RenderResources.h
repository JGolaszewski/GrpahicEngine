#pragma once

#include "rendering/include/renderer/Material.h"
#include "rendering/include/renderer/Mesh.h"

#include <DirectXMath.h>
#include <cstdint>
#include <memory>

namespace Engine::Renderer {
struct Submesh {
  std::shared_ptr<Mesh> mesh;
  std::shared_ptr<Material> material;
  DirectX::XMFLOAT4X4 LocalTransform = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  DirectX::XMFLOAT3 BoundsMin{};
  DirectX::XMFLOAT3 BoundsMax{};
  int32_t MaterialSlot = -1;
  mutable bool WorldBoundsCached = false;
  mutable DirectX::XMFLOAT4X4 CachedWorldTransform{};
  mutable DirectX::XMFLOAT3 CachedBoundsMin{};
  mutable DirectX::XMFLOAT3 CachedBoundsMax{};
  mutable DirectX::XMFLOAT3 CachedWorldBoundsCenter{};
  mutable float CachedWorldBoundsRadius = 0.0f;
};
} // namespace Engine::Renderer

#pragma once

#include "scene/include/ArchetypeWorld.h"

#include <DirectXMath.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Engine::Ecs {

/// Stable 128-bit identity stored in serialized scenes.
struct SceneGuid {
  uint64_t High = 0;
  uint64_t Low = 0;

  auto operator==(const SceneGuid&) const noexcept -> bool = default;
};

/// Persistent GUID assigned to an entity.
struct PersistentIdentity {
  SceneGuid Guid;
};

/// User-facing entity label.
struct Name {
  std::string Value;
};

/// Local translation, quaternion rotation, and scale.
struct alignas(16) LocalTransform {
  std::array<float, 3> Position = {0.0F, 0.0F, 0.0F};
  std::array<float, 4> Rotation = {0.0F, 0.0F, 0.0F, 1.0F};
  std::array<float, 3> Scale = {1.0F, 1.0F, 1.0F};
  bool IsDirty = true;
  std::array<std::byte, 7> Padding{};
};

/// Parent link maintained by Scene hierarchy operations.
struct Parent {
  EntityHandle Id = InvalidEntityHandle;
};

/// Intrusive child and sibling links maintained by Scene.
struct ChildList {
  EntityHandle FirstChild = InvalidEntityHandle;
  EntityHandle LastChild = InvalidEntityHandle;
  EntityHandle PreviousSibling = InvalidEntityHandle;
  EntityHandle NextSibling = InvalidEntityHandle;
};

/// Cached world-space transform computed from the hierarchy.
struct alignas(16) WorldTransform {
  DirectX::XMFLOAT4X4 Matrix = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                                0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
};

/// Per-entity visibility input.
struct LocalVisibility {
  bool IsVisible = true;
};

/// Visibility propagated through the hierarchy.
struct GlobalVisibility {
  bool IsVisible = true;
};

static_assert(alignof(LocalTransform) >= 16);
static_assert(alignof(WorldTransform) >= 16);

/// Rendering settings and model identity for a mesh entity.
struct MeshRenderer {
  std::string ModelGuid;
  std::string PrimitiveType;
  bool CastShadows = true;
  bool ReceiveShadows = true;
  bool Visible = true;
  bool Wireframe = false;
  bool DisableDepthTest = false;
};

/// Parameters for a spot light attached to an entity.
struct SpotLight {
  std::array<float, 3> Direction = {0.0F, -1.0F, 0.0F};
  std::array<float, 3> Color = {1.0F, 1.0F, 1.0F};
  float Intensity = 1.0F;
  float Range = 10.0F;
  float InnerAngleDegrees = 20.0F;
  float OuterAngleDegrees = 30.0F;
  float ShadowBias = 0.0015F;
  float ShadowNormalBias = 0.0015F;
  bool UsePosition = true;
  bool CastShadows = true;
};

/// Projection and activation settings for an entity camera.
struct Camera {
  float FieldOfViewDegrees = 45.0F;
  float NearPlane = 0.1F;
  float FarPlane = 1000.0F;
  int32_t Priority = 0;
  bool Active = true;
};

inline auto Validate(const LocalTransform& component) -> Status {
  for (float value : component.Position) {
    if (!std::isfinite(value)) {
      return std::unexpected(MakeError("Transform position must contain finite values"));
    }
  }
  float rotationLengthSquared = 0.0F;
  for (float value : component.Rotation) {
    if (!std::isfinite(value)) {
      return std::unexpected(MakeError("Transform rotation must contain finite values"));
    }
    rotationLengthSquared += value * value;
  }
  if (rotationLengthSquared < 1.0e-8F) {
    return std::unexpected(MakeError("Transform rotation quaternion must be nonzero"));
  }
  for (float value : component.Scale) {
    if (!std::isfinite(value) || value <= 0.0F) {
      return std::unexpected(MakeError("Transform scale must contain finite positive values"));
    }
  }
  return {};
}

inline auto Validate(const SpotLight& component) -> Status {
  for (float value : component.Direction) {
    if (!std::isfinite(value)) {
      return std::unexpected(MakeError("Spot light direction must contain finite values"));
    }
  }
  for (float value : component.Color) {
    if (!std::isfinite(value) || value < 0.0F) {
      return std::unexpected(MakeError("Spot light color must contain finite nonnegative values"));
    }
  }
  if (!std::isfinite(component.Intensity) || component.Intensity < 0.0F || !std::isfinite(component.Range) ||
      component.Range <= 0.0F || !std::isfinite(component.InnerAngleDegrees) ||
      !std::isfinite(component.OuterAngleDegrees) || component.InnerAngleDegrees < 0.0F ||
      component.OuterAngleDegrees <= component.InnerAngleDegrees || component.OuterAngleDegrees >= 90.0F) {
    return std::unexpected(MakeError("Spot light range, intensity, or cone angles are invalid"));
  }
  return {};
}

inline auto Validate(const Camera& component) -> Status {
  if (!std::isfinite(component.FieldOfViewDegrees) || component.FieldOfViewDegrees <= 0.0F ||
      component.FieldOfViewDegrees >= 180.0F || !std::isfinite(component.NearPlane) || component.NearPlane <= 0.0F ||
      !std::isfinite(component.FarPlane) || component.FarPlane <= component.NearPlane) {
    return std::unexpected(MakeError("Camera field of view and clip planes are invalid"));
  }
  return {};
}

} // namespace Engine::Ecs

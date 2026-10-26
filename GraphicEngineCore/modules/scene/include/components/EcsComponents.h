#pragma once

#include "scene/include/ArchetypeWorld.h"
#include "scene/include/SceneDocument.h"
#include "scene/include/components/CameraPostProcessing.h"

#include <DirectXMath.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Engine::Ecs {

/// Stable 128-bit identity stored in serialized scenes.
struct SceneGuid {
  uint64_t High = 0; ///< Most-significant 64 bits of the identity.
  uint64_t Low = 0;  ///< Least-significant 64 bits of the identity.

  /// Compares both words of the persistent identity.
  /// @param other Identity to compare with.
  /// @return True when both identity words match.
  auto operator==(const SceneGuid& other) const noexcept -> bool = default;
};

/// Persistent GUID assigned to an entity.
struct PersistentIdentity {
  SceneGuid Guid; ///< Stable identity persisted with the entity.
};

/// User-facing entity label.
struct Name {
  std::string Value; ///< User-facing entity label.
};

/// Marks the permanent root entity that owns a scene hierarchy.
struct SceneRoot {};

/// Local translation, quaternion rotation, and scale.
struct alignas(16) LocalTransform {
  std::array<float, 3> Position = {0.0F, 0.0F, 0.0F};       ///< Local translation.
  std::array<float, 4> Rotation = {0.0F, 0.0F, 0.0F, 1.0F}; ///< Local XYZW rotation quaternion.
  std::array<float, 3> Scale = {1.0F, 1.0F, 1.0F};          ///< Positive local scale per axis.
  bool FlipHorizontal = false;                              ///< Mirrors geometry across the local X axis.
  bool FlipVertical = false;                                ///< Mirrors geometry across the local Y axis.
  bool IsDirty = true;                                      ///< Whether the cached world transform needs recomputation.
  std::array<std::byte, 5> Padding{}; ///< Padding that preserves the component's 16-byte alignment.
};

/// Parent link maintained by Scene hierarchy operations.
struct Parent {
  EntityHandle Id = InvalidEntityHandle; ///< Parent entity, or the invalid handle for a root.
};

/// Intrusive child and sibling links maintained by Scene.
struct ChildList {
  EntityHandle FirstChild = InvalidEntityHandle;      ///< First child in sibling order.
  EntityHandle LastChild = InvalidEntityHandle;       ///< Last child in sibling order.
  EntityHandle PreviousSibling = InvalidEntityHandle; ///< Previous entity with the same parent.
  EntityHandle NextSibling = InvalidEntityHandle;     ///< Next entity with the same parent.
};

/// Cached world-space transform computed from the hierarchy.
struct alignas(16) WorldTransform {
  DirectX::XMFLOAT4X4 Matrix = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                                0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F}; ///< Cached world matrix.
  bool HasAxisFlip = false;            ///< Whether this entity or an ancestor has an authored axis flip.
  std::array<std::byte, 15> Padding{}; ///< Padding that preserves the component's 16-byte alignment.
};

/// Per-entity visibility input.
struct LocalVisibility {
  bool IsVisible = true; ///< Local visibility setting before hierarchy propagation.
};

/// Visibility propagated through the hierarchy.
struct GlobalVisibility {
  bool IsVisible = true; ///< Effective visibility after evaluating parent visibility.
};

static_assert(alignof(LocalTransform) >= 16);
static_assert(alignof(WorldTransform) >= 16);

/// Rendering settings and model identity for a mesh entity.
struct MeshRenderer {
  std::string ModelGuid;                         ///< Imported model identity, when using a model asset.
  std::string PrimitiveType;                     ///< Built-in primitive name, when not using a model asset.
  std::string MaterialGuid;                      ///< Optional reusable material asset identity.
  bool CastShadows = true;                       ///< Whether this mesh contributes to shadow maps.
  bool ReceiveShadows = true;                    ///< Whether this mesh receives shadows.
  bool Visible = true;                           ///< Whether this mesh is submitted for rendering.
  bool Wireframe = false;                        ///< Whether this mesh uses wireframe rasterization.
  bool DisableDepthTest = false;                 ///< Whether depth testing is disabled for this mesh.
  std::optional<SceneMaterialDocument> Material; ///< Optional material override applied to all mesh slots.
  std::vector<std::optional<SceneMaterialDocument>> MaterialOverrides; ///< Optional per-submesh material overrides.
};

/// Supported authored light types.
enum class LightType : uint8_t { Directional, Point, Spot };

/// Parameters for a directional, point, or spot light attached to an entity.
struct Light {
  LightType Type = LightType::Spot;                     ///< Light shape used by the renderer.
  std::array<float, 3> Direction = {0.0F, -1.0F, 0.0F}; ///< Local emission direction.
  std::array<float, 3> Color = {1.0F, 1.0F, 1.0F};      ///< Linear RGB light color.
  float Intensity = 1.0F;                               ///< Radiometric intensity multiplier.
  float Range = 10.0F;                                  ///< Maximum light influence distance.
  float InnerAngleDegrees = 20.0F;                      ///< Inner spotlight cone angle in degrees.
  float OuterAngleDegrees = 30.0F;                      ///< Outer spotlight cone angle in degrees.
  float ShadowBias = 0.0015F;                           ///< Constant shadow-map depth bias.
  float ShadowNormalBias = 0.0015F;                     ///< Slope-scaled shadow-map bias.
  bool CastShadows = true;                              ///< Whether the light produces shadows.
};
/// Source-compatible alias for the previous spot-light component name.
using SpotLight = Light;

/// Projection, output resolution, and activation settings for an authored scene camera.
struct Camera {
  float FieldOfViewDegrees = 45.0F; ///< Vertical field of view in degrees.
  float NearPlane = 0.1F;           ///< Near clipping plane distance.
  float FarPlane = 1000.0F;         ///< Far clipping plane distance.
  uint32_t OutputWidth = 1920;      ///< Configured render output width in pixels; must be positive.
  uint32_t OutputHeight = 1080;     ///< Configured render output height in pixels; must be positive.
  int32_t Priority = 0;             ///< Selection priority when choosing an active scene camera.
  bool Active = true;               ///< Whether the camera participates in camera selection.
  std::optional<AmbientLightSettings> AmbientLightOverride; ///< Optional ambient light replacing the scene setting.
};

/// Validates finite local transform components and positive scale values.
/// @param component Transform to validate.
/// @return Success, or an error describing the invalid value.
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

/// Validates finite light values and the fields applicable to the selected light type.
/// @param component Light component to validate.
/// @return Success, or an error describing the invalid setting.
inline auto Validate(const Light& component) -> Status {
  if (component.Type != LightType::Directional && component.Type != LightType::Point &&
      component.Type != LightType::Spot) {
    return std::unexpected(MakeError("Light type is unsupported"));
  }
  float directionLengthSquared = 0.0F;
  for (float value : component.Direction) {
    if (!std::isfinite(value)) {
      return std::unexpected(MakeError("Light direction must contain finite values"));
    }
    directionLengthSquared += value * value;
  }
  if (!std::isfinite(directionLengthSquared)) {
    return std::unexpected(MakeError("Light direction magnitude must be finite"));
  }
  if (component.Type != LightType::Point && directionLengthSquared <= 1.0e-12F) {
    return std::unexpected(MakeError("Light direction must be nonzero"));
  }
  for (float value : component.Color) {
    if (!std::isfinite(value) || value < 0.0F) {
      return std::unexpected(MakeError("Light color must contain finite nonnegative values"));
    }
  }
  if (!std::isfinite(component.Intensity) || component.Intensity < 0.0F) {
    return std::unexpected(MakeError("Light intensity must be finite and nonnegative"));
  }
  if (component.Type != LightType::Directional && (!std::isfinite(component.Range) || component.Range <= 0.0F)) {
    return std::unexpected(MakeError("Point and spot light ranges must be finite and positive"));
  }
  if (component.Type == LightType::Spot &&
      (!std::isfinite(component.InnerAngleDegrees) || !std::isfinite(component.OuterAngleDegrees) ||
       component.InnerAngleDegrees < 0.0F || component.OuterAngleDegrees <= component.InnerAngleDegrees ||
       component.OuterAngleDegrees >= 90.0F)) {
    return std::unexpected(MakeError("Spot light cone angles are invalid"));
  }
  if (!std::isfinite(component.ShadowBias) || component.ShadowBias < 0.0F ||
      !std::isfinite(component.ShadowNormalBias) || component.ShadowNormalBias < 0.0F) {
    return std::unexpected(MakeError("Light shadow bias values must be finite and nonnegative"));
  }
  return {};
}

/// Validates camera projection planes, field of view, and output dimensions.
/// @param component Camera component to validate.
/// @return Success, or an error describing the invalid setting.
inline auto Validate(const Camera& component) -> Status {
  if (!std::isfinite(component.FieldOfViewDegrees) || component.FieldOfViewDegrees <= 0.0F ||
      component.FieldOfViewDegrees >= 180.0F || !std::isfinite(component.NearPlane) || component.NearPlane <= 0.0F ||
      !std::isfinite(component.FarPlane) || component.FarPlane <= component.NearPlane || component.OutputWidth == 0 ||
      component.OutputHeight == 0) {
    return std::unexpected(MakeError("Camera projection, clip planes, or output resolution are invalid"));
  }
  if (component.AmbientLightOverride) {
    const Status ambientLightValidation = Engine::Validate(*component.AmbientLightOverride);
    if (!ambientLightValidation) {
      return ambientLightValidation;
    }
  }
  return {};
}

} // namespace Engine::Ecs

#pragma once

#include "runtime/include/CoreExport.h"
#include "scene/include/components/EcsComponents.h"

#include <array>
#include <optional>

namespace Engine::Renderer {

/// Camera pose and projection parameters consumed by a render view.
///
/// The quaternion is stored in x, y, z, w order. Resolution and aspect ratio
/// belong to the render target, so the same camera can be previewed at editor
/// viewport dimensions or rendered at an explicit output size.
struct ENGINE_API CameraView {
  std::array<float, 3> Position = {0.0F, 0.0F, 0.0F};
  std::array<float, 4> Rotation = {0.0F, 0.0F, 0.0F, 1.0F};
  float FieldOfViewDegrees = 60.0F;
  float NearPlane = 0.1F;
  float FarPlane = 1000.0F;
};

/// Resolves an authored scene camera and its world transform into renderer parameters.
/// @param camera Scene-camera projection settings.
/// @param transform Current world-space camera transform.
/// @return Camera parameters, or no value when the transform cannot be decomposed.
ENGINE_API auto ResolveSceneCameraView(const Engine::Ecs::Camera& camera,
                                       const Engine::Ecs::WorldTransform& transform) noexcept
    -> std::optional<CameraView>;

} // namespace Engine::Renderer

#pragma once

#include "runtime/include/CoreExport.h"

#include <array>

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

} // namespace Engine::Renderer

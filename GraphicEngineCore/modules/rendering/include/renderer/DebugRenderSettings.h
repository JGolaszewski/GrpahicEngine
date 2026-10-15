#pragma once

#include "scene/include/SceneIds.h"

#include <cstdint>

namespace Engine::Renderer {
enum class DebugDepthMode : uint8_t { Test, AlwaysOnTop };

struct DebugRenderSettings {
  bool ShowLightGizmos = false;
  /// Draws visible authored camera frustums in editor views; final renders omit debug geometry.
  bool ShowCameraFrustums = false;
  bool ShowWireframePlane = false;
  DebugDepthMode DepthMode = DebugDepthMode::AlwaysOnTop;
  Engine::LightId SelectedLightId = Engine::InvalidLightId;
};
} // namespace Engine::Renderer

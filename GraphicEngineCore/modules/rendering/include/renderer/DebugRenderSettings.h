#pragma once

#include "scene/include/SceneIds.h"

#include <cstdint>

namespace Engine::Renderer {
enum class DebugDepthMode : uint8_t { Test, AlwaysOnTop };

/// Selects which editor-only debug geometry is submitted for a render view.
struct DebugRenderSettings {
  bool ShowLightGizmos = false; ///< Whether to draw light gizmos.
  /// Draws visible authored camera frustums in editor views; final renders omit debug geometry.
  bool ShowCameraFrustums = false;
  bool ShowWireframePlane = false; ///< Whether to draw the editor wireframe ground plane.
  DebugDepthMode DepthMode = DebugDepthMode::AlwaysOnTop; ///< Depth behavior for debug geometry.
  Engine::LightId SelectedLightId = Engine::InvalidLightId; ///< Light whose gizmo is shown as selected.
};
} // namespace Engine::Renderer

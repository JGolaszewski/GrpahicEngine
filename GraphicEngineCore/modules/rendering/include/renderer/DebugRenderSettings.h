#pragma once

#include "scene/include/SceneIds.h"

#include <cstdint>

namespace Engine::Renderer
{
/// Depth behavior for overlay and construction-plane debug geometry.
enum class DebugDepthMode : uint8_t
{
    Test,
    AlwaysOnTop
};

/// Per-view controls for editor debug rendering.
struct DebugRenderSettings
{
    bool ShowLightGizmos = false;    ///< Whether spotlight cones and positions are drawn.
    bool ShowWireframePlane = false; ///< Whether the world construction plane is drawn.
    DebugDepthMode DepthMode = DebugDepthMode::AlwaysOnTop;   ///< Depth behavior for debug primitives.
    Engine::LightId SelectedLightId = Engine::InvalidLightId; ///< Light to highlight, or InvalidLightId.
};
} // namespace Engine::Renderer

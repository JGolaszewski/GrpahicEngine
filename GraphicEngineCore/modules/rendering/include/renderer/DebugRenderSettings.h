#pragma once

#include "scene/include/SceneIds.h"

#include <cstdint>

namespace Engine::Renderer
{
enum class DebugDepthMode : uint8_t
{
    Test,
    AlwaysOnTop
};

struct DebugRenderSettings
{
    bool ShowLightGizmos = false;
    bool ShowWireframePlane = false;
    DebugDepthMode DepthMode = DebugDepthMode::AlwaysOnTop;
    Engine::LightId SelectedLightId = Engine::InvalidLightId;
};
} // namespace Engine::Renderer

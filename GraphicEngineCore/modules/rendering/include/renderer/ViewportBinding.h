#pragma once

#include "runtime/include/CoreExport.h"
#include "rendering/include/renderer/DebugRenderSettings.h"

namespace Engine::Renderer
{
class Camera;
}
namespace Engine
{
class Viewport;
class Scene;
} // namespace Engine

namespace Engine
{

/// Associates a scene and camera with a viewport for rendering.
struct ENGINE_API ViewportBinding
{
    ::Engine::Renderer::Camera *SourceCamera = nullptr; ///< Camera used to render the scene.
    Engine::Viewport *TargetViewport = nullptr;         ///< Viewport that receives the rendered image.
    Engine::Scene *TargetScene = nullptr;               ///< Scene whose objects are submitted.

    ::Engine::Renderer::DebugRenderSettings DebugSettings; ///< Optional debug visualizations for this view.

    /// Checks that the required camera, viewport, and scene are present.
    /// @return True when the binding can be submitted.
    bool IsValid() const
    {
        return SourceCamera != nullptr && TargetViewport != nullptr && TargetScene != nullptr;
    }
};
} // namespace Engine

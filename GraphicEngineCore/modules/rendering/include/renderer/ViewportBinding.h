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

struct ENGINE_API ViewportBinding
{
    ::Engine::Renderer::Camera *SourceCamera = nullptr;
    Engine::Viewport *TargetViewport = nullptr;
    Engine::Scene *TargetScene = nullptr;

    ::Engine::Renderer::DebugRenderSettings DebugSettings;

    bool IsValid() const
    {
        return SourceCamera != nullptr && TargetViewport != nullptr && TargetScene != nullptr;
    }
};
} // namespace Engine

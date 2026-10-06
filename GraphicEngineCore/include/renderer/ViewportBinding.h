#pragma once

#include "CoreExport.h"
#include "renderer/DebugRenderSettings.h"

namespace Renderer
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
    Renderer::Camera *SourceCamera = nullptr;
    Engine::Viewport *TargetViewport = nullptr;
    Engine::Scene *TargetScene = nullptr;

    Renderer::DebugRenderSettings DebugSettings;

    bool IsValid() const
    {
        return SourceCamera != nullptr && TargetViewport != nullptr && TargetScene != nullptr;
    }
};
} // namespace Engine

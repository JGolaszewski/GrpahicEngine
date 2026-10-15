#pragma once

#include "rendering/include/renderer/DebugRenderSettings.h"
#include "runtime/include/CoreExport.h"
#include "scene/include/ArchetypeWorld.h"

namespace Engine {
class Viewport;
class Scene;
} // namespace Engine

namespace Engine {

struct ENGINE_API ViewportBinding {
  Engine::EntityHandle CameraEntity = Engine::InvalidEntityHandle;
  Engine::Viewport* TargetViewport = nullptr;
  Engine::Scene* TargetScene = nullptr;

  ::Engine::Renderer::DebugRenderSettings DebugSettings;

  bool IsValid() const {
    return CameraEntity != Engine::InvalidEntityHandle && TargetViewport != nullptr && TargetScene != nullptr;
  }
};
} // namespace Engine

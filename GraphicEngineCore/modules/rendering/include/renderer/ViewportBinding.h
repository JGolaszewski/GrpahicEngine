#pragma once

#include "rendering/include/renderer/CameraView.h"
#include "rendering/include/renderer/DebugRenderSettings.h"
#include "runtime/include/CoreExport.h"
#include "scene/include/ArchetypeWorld.h"

#include <cstdint>

namespace Engine {
class Viewport;
class Scene;
} // namespace Engine

namespace Engine {

struct ENGINE_API ViewportBinding {
  enum class Mode : uint8_t { Editor, Final };

  Renderer::CameraView Camera;
  Mode RenderMode = Mode::Editor;
  /// Deprecated ECS camera reference retained while existing editor callers migrate to CameraView.
  Engine::EntityHandle CameraEntity = Engine::InvalidEntityHandle;
  Engine::Viewport* TargetViewport = nullptr;
  Engine::Scene* TargetScene = nullptr;
  uint32_t OutputWidth = 0;
  uint32_t OutputHeight = 0;

  ::Engine::Renderer::DebugRenderSettings DebugSettings;

  bool IsValid() const {
    return TargetViewport != nullptr && TargetScene != nullptr &&
           (RenderMode != Mode::Final || (OutputWidth > 0 && OutputHeight > 0));
  }
};
} // namespace Engine

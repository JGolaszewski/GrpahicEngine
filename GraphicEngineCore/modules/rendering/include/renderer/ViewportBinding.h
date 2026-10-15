#pragma once

#include "rendering/include/renderer/CameraView.h"
#include "rendering/include/renderer/DebugRenderSettings.h"
#include "rendering/include/renderer/Viewport.h"
#include "runtime/include/CoreExport.h"
#include "scene/include/ArchetypeWorld.h"

#include <cstdint>

namespace Engine {
class Viewport;
class Scene;
} // namespace Engine

namespace Engine {

struct ENGINE_API ViewportBinding {
  /// Rendering behavior for an individual submitted view.
  enum class Mode : uint8_t {
    /// Editor viewport output, including configured editor-only debug geometry.
    Editor,
    /// Single-color camera preview; editor-only debug geometry is omitted.
    Preview,
    /// Full-resolution output with the final-render AOV attachments and no editor-only geometry.
    Final
  };

  Renderer::CameraView Camera;
  Mode RenderMode = Mode::Editor;
  /// Deprecated ECS camera reference retained while existing editor callers migrate to CameraView.
  Engine::EntityHandle CameraEntity = Engine::InvalidEntityHandle;
  Engine::Viewport* TargetViewport = nullptr;
  Engine::Scene* TargetScene = nullptr;
  /// Final-output dimensions; for Mode::Final they must match the target viewport.
  uint32_t OutputWidth = 0;
  uint32_t OutputHeight = 0;

  ::Engine::Renderer::DebugRenderSettings DebugSettings;

  bool IsValid() const {
    if (TargetViewport == nullptr || TargetScene == nullptr || TargetViewport->GetFramebufferHandle() == nullptr ||
        TargetViewport->GetWidth() == 0 || TargetViewport->GetHeight() == 0) {
      return false;
    }
    return RenderMode != Mode::Final ||
           (OutputWidth == TargetViewport->GetWidth() && OutputHeight == TargetViewport->GetHeight());
  }
};
} // namespace Engine

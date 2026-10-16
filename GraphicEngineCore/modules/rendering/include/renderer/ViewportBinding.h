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

/// Binds a scene and camera to an editor viewport or final-render output.
struct ENGINE_API ViewportBinding {
  /// Selects editor preview behavior or final-render behavior.
  enum class Mode : uint8_t { Editor, Final };

  Renderer::CameraView Camera; ///< Resolved camera parameters for this view.
  Mode RenderMode = Mode::Editor; ///< Rendering mode applied to the view.
  /// Deprecated ECS camera reference retained while existing editor callers migrate to CameraView.
  Engine::EntityHandle CameraEntity = Engine::InvalidEntityHandle; ///< Deprecated authored camera handle.
  Engine::Viewport* TargetViewport = nullptr; ///< Destination viewport, when rendering an editor preview.
  Engine::Scene* TargetScene = nullptr; ///< Scene supplying renderable objects.
  uint32_t OutputWidth = 0; ///< Explicit width for final renders.
  uint32_t OutputHeight = 0; ///< Explicit height for final renders.

  ::Engine::Renderer::DebugRenderSettings DebugSettings; ///< Debug geometry settings for editor views.

  /// Checks whether required targets and final-render dimensions are valid.
  /// @return True when the binding can be submitted to the renderer.
  bool IsValid() const {
    return TargetViewport != nullptr && TargetScene != nullptr &&
           (RenderMode != Mode::Final || (OutputWidth > 0 && OutputHeight > 0));
  }
};
} // namespace Engine

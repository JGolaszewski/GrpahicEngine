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

/// Binds a scene and camera to an editor viewport or final-render output.
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

  Renderer::CameraView Camera;    ///< Resolved camera parameters for this view.
  Mode RenderMode = Mode::Editor; ///< Rendering mode applied to the view.
  bool SnapshotCapture = false;   ///< Enables snapshot-only ID and overdraw passes for a final render.
  /// Deprecated ECS camera reference retained while existing editor callers migrate to CameraView.
  Engine::EntityHandle CameraEntity = Engine::InvalidEntityHandle; ///< Deprecated authored camera handle.
  Engine::Viewport* TargetViewport = nullptr; ///< Destination viewport, when rendering an editor preview.
  Engine::Scene* TargetScene = nullptr;       ///< Scene supplying renderable objects.
  uint32_t OutputWidth = 0;  ///< Explicit final-output width; for Mode::Final it must match the target viewport.
  uint32_t OutputHeight = 0; ///< Explicit final-output height; for Mode::Final it must match the target viewport.

  ::Engine::Renderer::DebugRenderSettings DebugSettings; ///< Debug geometry settings for editor views.

  /// Checks whether required targets and final-render dimensions are valid.
  /// @return True when the binding can be submitted to the renderer.
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

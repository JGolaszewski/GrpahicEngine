#pragma once

#include "rendering/include/renderer/CameraView.h"
#include "rendering/include/renderer/DebugRenderSettings.h"
#include "rendering/include/renderer/Viewport.h"
#include "runtime/include/CoreExport.h"
#include "scene/include/ArchetypeWorld.h"
#include "scene/include/components/CameraPostProcessing.h"

#include <cstdint>

namespace Engine {
class Viewport;
class Scene;
} // namespace Engine

namespace Engine {

/// Selects the environment source used for indirect lighting in a rendered view.
enum class EnvironmentPreset : uint8_t {
  Disabled, ///< Do not add environment lighting.
  Studio    ///< Use the renderer's built-in studio environment.
};

/// Per-view environment lighting configuration.
struct EnvironmentLightingSettings {
  EnvironmentPreset Preset = EnvironmentPreset::Disabled; ///< Environment source for this view.
  float Intensity = 1.0F; ///< Non-negative scale applied to indirect diffuse and specular lighting.
};

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
  /// Deprecated ECS camera reference retained while existing editor callers migrate to CameraView.
  Engine::EntityHandle CameraEntity = Engine::InvalidEntityHandle; ///< Deprecated authored camera handle.
  Engine::Viewport* TargetViewport = nullptr; ///< Destination viewport, when rendering an editor preview.
  Engine::Scene* TargetScene = nullptr;       ///< Scene supplying renderable objects.
  uint32_t OutputWidth = 0;  ///< Explicit final-output width; for Mode::Final it must match the target viewport.
  uint32_t OutputHeight = 0; ///< Explicit final-output height; for Mode::Final it must match the target viewport.

  ::Engine::Renderer::DebugRenderSettings DebugSettings; ///< Debug geometry settings for editor views.
  EnvironmentLightingSettings Environment;               ///< Environment source and intensity for this view.
  Engine::Ecs::CameraPostProcessing PostProcessing; ///< Authored camera's lighting settings and ordered color effects.

  /// Checks required targets, final-render dimensions, and camera effect settings.
  /// @return True when the binding can be submitted to the renderer.
  bool IsValid() const {
    if (!Engine::Ecs::Validate(PostProcessing) || TargetViewport == nullptr || TargetScene == nullptr ||
        TargetViewport->GetFramebufferHandle() == nullptr || TargetViewport->GetWidth() == 0 ||
        TargetViewport->GetHeight() == 0) {
      return false;
    }
    return RenderMode != Mode::Final ||
           (OutputWidth == TargetViewport->GetWidth() && OutputHeight == TargetViewport->GetHeight());
  }
};
} // namespace Engine

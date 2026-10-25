#pragma once

#include "rendering/include/renderer/CameraView.h"
#include "scene/include/ArchetypeWorld.h"

#include <cstdint>

/// Camera and output configuration resolved for one full-resolution viewer refresh.
struct CameraPreviewRequest {
  Engine::Renderer::CameraView View;
  uint32_t Width = 0;
  uint32_t Height = 0;
  Engine::Ecs::CameraPostProcessing PostProcessing;
};

/// Resolve the requested entity's current camera state for a viewer render.
inline auto ResolveCameraPreviewRequest(const Engine::ArchetypeWorld& world, Engine::EntityHandle entity)
    -> Engine::Result<CameraPreviewRequest> {
  const auto* camera = world.Get<Engine::Ecs::Camera>(entity);
  const auto* transform = world.Get<Engine::Ecs::WorldTransform>(entity);
  if (camera == nullptr || transform == nullptr || camera->OutputWidth == 0 || camera->OutputHeight == 0) {
    return std::unexpected(Engine::MakeError("The selected entity has no valid scene camera"));
  }
  const auto view = Engine::Renderer::ResolveSceneCameraView(*camera, *transform);
  if (!view) {
    return std::unexpected(Engine::MakeError("The selected camera transform cannot be rendered"));
  }
  CameraPreviewRequest request{.View = *view, .Width = camera->OutputWidth, .Height = camera->OutputHeight};
  if (const auto* postProcessing = world.Get<Engine::Ecs::CameraPostProcessing>(entity); postProcessing != nullptr) {
    request.PostProcessing = *postProcessing;
  }
  return request;
}

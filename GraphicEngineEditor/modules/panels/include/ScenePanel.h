#pragma once

#include "events/include/EditorEvents.h"
#include "panels/include/EditorPanel.h"
#include <cstdint>
#include <scene/include/ArchetypeWorld.h>
#include <utility>

// ================================================================================
// ScenePanel.h
// ================================================================================
// Decoupled from the Editor. Receives ViewportChangedEvent to know the
// current viewport texture ID and size. Emits ViewportResizeRequestedEvent
// when the ImGui window size changes, and ViewportHoveredEvent when the
// hover state changes.
// ================================================================================

class ScenePanel : public EditorPanel {
public:
  ScenePanel() = default;

  void Draw() override;
  void OnEvent(Engine::Events::Event& event) override;

private:
  void* m_TextureId = nullptr;
  void* m_CameraPreviewTextureId = nullptr;
  Engine::EntityHandle m_CameraPreviewEntity = Engine::InvalidEntityHandle;
  std::pair<float, float> m_CameraPreviewSize = {0.0f, 0.0f};
  float m_CameraPreviewScale = 0.35F;
  std::pair<float, float> m_ViewportSize = {0.0f, 0.0f};
  bool m_IsHovered = false;
};

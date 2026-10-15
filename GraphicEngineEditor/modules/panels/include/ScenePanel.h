#pragma once

#include "events/include/EditorEvents.h"
#include "panels/include/EditorPanel.h"
#include <cstdint>
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
  std::pair<float, float> m_ViewportSize = {0.0f, 0.0f};
  bool m_IsHovered = false;
};

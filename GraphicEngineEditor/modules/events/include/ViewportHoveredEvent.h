#pragma once

#include "runtime/include/events/Event.h"

// ================================================================================
// ViewportHoveredEvent.h
// ================================================================================
// Emitted by the ScenePanel when the ImGui viewport window's hover state
// changes. The Editor receives this to drive camera controls (e.g. WASD only
// when the viewport is hovered).
// ================================================================================

class ViewportHoveredEvent : public Engine::Events::Event {
public:
  explicit ViewportHoveredEvent(bool hovered) : m_Hovered(hovered) {}

  static Engine::Events::EventType GetStaticType() { return Engine::Events::EventType::ViewportHovered; }

  Engine::Events::EventType GetType() const override { return GetStaticType(); }

  [[nodiscard]] bool IsHovered() const noexcept { return m_Hovered; }

private:
  bool m_Hovered;
};

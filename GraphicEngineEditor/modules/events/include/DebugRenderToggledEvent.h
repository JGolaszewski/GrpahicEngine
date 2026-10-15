#pragma once

#include "runtime/include/events/Event.h"

// ================================================================================
// DebugRenderToggledEvent.h
// ================================================================================
// Emitted by the InspectorPanel when the user toggles debug-render options
// (e.g. wireframe gizmos, wireframe plane). The Editor receives this and
// updates its internal flags so that OnRender can show/hide the debug geometry.
// ================================================================================

class DebugRenderToggledEvent : public Engine::Events::Event {
public:
  enum class Toggle { LightGizmos, WireframePlane };

  DebugRenderToggledEvent(Toggle toggle, bool value) : m_Toggle(toggle), m_Value(value) {}

  static Engine::Events::EventType GetStaticType() { return Engine::Events::EventType::DebugRenderToggled; }

  Engine::Events::EventType GetType() const override { return GetStaticType(); }

  [[nodiscard]] Toggle GetToggle() const noexcept { return m_Toggle; }
  [[nodiscard]] bool GetValue() const noexcept { return m_Value; }

private:
  Toggle m_Toggle;
  bool m_Value;
};

#pragma once

#include "runtime/include/events/Event.h"

class CommandHistoryActionRequestedEvent final : public Engine::Events::Event {
public:
  enum class Action { Undo, Redo };
  explicit CommandHistoryActionRequestedEvent(Action action) : m_Action(action) {}
  static auto GetStaticType() -> Engine::Events::EventType {
    return Engine::Events::EventType::CommandHistoryActionRequested;
  }
  auto GetType() const -> Engine::Events::EventType override { return GetStaticType(); }
  [[nodiscard]] auto GetAction() const noexcept -> Action { return m_Action; }

private:
  Action m_Action;
};

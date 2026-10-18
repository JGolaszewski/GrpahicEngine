#pragma once

#include "runtime/include/events/Event.h"

#include <string>
#include <utility>

class AssetWorkflowFeedbackEvent final : public Engine::Events::Event {
public:
  AssetWorkflowFeedbackEvent(bool success, std::string message) : m_Success(success), m_Message(std::move(message)) {}
  static Engine::Events::EventType GetStaticType() { return Engine::Events::EventType::AssetWorkflowFeedback; }
  Engine::Events::EventType GetType() const override { return GetStaticType(); }
  bool Succeeded() const noexcept { return m_Success; }
  const std::string& GetMessage() const noexcept { return m_Message; }

private:
  bool m_Success;
  std::string m_Message;
};

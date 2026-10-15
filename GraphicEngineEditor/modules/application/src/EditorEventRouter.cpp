#include "application/include/EditorEventRouter.h"

#include <algorithm>
#include <utility>

EditorEventRouter::Connection::~Connection() { Reset(); }

EditorEventRouter::Connection::Connection(Connection&& other) noexcept
    : m_Router(std::exchange(other.m_Router, nullptr)), m_Id(std::exchange(other.m_Id, 0)) {}

auto EditorEventRouter::Connection::operator=(Connection&& other) noexcept -> EditorEventRouter::Connection& {
  if (this != &other) {
    Reset();
    m_Router = std::exchange(other.m_Router, nullptr);
    m_Id = std::exchange(other.m_Id, 0);
  }
  return *this;
}

void EditorEventRouter::Connection::Reset() noexcept {
  if (m_Router != nullptr) {
    m_Router->Unsubscribe(m_Id);
    m_Router = nullptr;
    m_Id = 0;
  }
}

auto EditorEventRouter::Subscribe(EditorPanel& panel, RequestHandler requestHandler) -> EditorEventRouter::Connection {
  const uint64_t id = m_NextId++;
  panel.SetRequestSink(std::move(requestHandler));
  m_Subscribers.push_back({.Id = id, .Panel = &panel});
  return {this, id};
}

void EditorEventRouter::DispatchInput(Engine::Events::Event& event) {
  for (const uint64_t id : SnapshotSubscriberIds()) {
    const auto found =
        std::ranges::find_if(m_Subscribers, [id](const Subscriber& subscriber) -> bool { return subscriber.Id == id; });
    if (found == m_Subscribers.end()) {
      continue;
    }
    found->Panel->OnEvent(event);
    if (event.Handled) {
      return;
    }
  }
}

void EditorEventRouter::NotifyPanels(Engine::Events::Event& event) {
  for (const uint64_t id : SnapshotSubscriberIds()) {
    const auto found =
        std::ranges::find_if(m_Subscribers, [id](const Subscriber& subscriber) -> bool { return subscriber.Id == id; });
    if (found != m_Subscribers.end()) {
      found->Panel->OnEvent(event);
    }
  }
}

auto EditorEventRouter::SnapshotSubscriberIds() const -> std::vector<uint64_t> {
  std::vector<uint64_t> ids;
  ids.reserve(m_Subscribers.size());
  for (const auto& subscriber : m_Subscribers) {
    ids.push_back(subscriber.Id);
  }
  return ids;
}

void EditorEventRouter::Unsubscribe(uint64_t id) noexcept {
  const auto found =
      std::ranges::find_if(m_Subscribers, [id](const Subscriber& subscriber) -> bool { return subscriber.Id == id; });
  if (found != m_Subscribers.end()) {
    found->Panel->SetRequestSink({});
    m_Subscribers.erase(found);
  }
}

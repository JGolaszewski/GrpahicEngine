#pragma once

#include "runtime/include/events/Event.h"
#include "scene/include/ArchetypeWorld.h"

class EcsEntityDeleteRequestedEvent final : public Engine::Events::Event {
public:
  explicit EcsEntityDeleteRequestedEvent(Engine::EntityHandle entity) : m_Entity(entity) {}

  static auto GetStaticType() -> Engine::Events::EventType {
    return Engine::Events::EventType::EcsEntityDeleteRequested;
  }
  auto GetType() const -> Engine::Events::EventType override { return GetStaticType(); }
  auto GetEntity() const noexcept -> Engine::EntityHandle { return m_Entity; }

private:
  Engine::EntityHandle m_Entity;
};

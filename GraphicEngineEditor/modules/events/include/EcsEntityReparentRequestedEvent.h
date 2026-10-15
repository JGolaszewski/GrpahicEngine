#pragma once

#include "runtime/include/events/Event.h"
#include "scene/include/ArchetypeWorld.h"

class EcsEntityReparentRequestedEvent final : public Engine::Events::Event {
public:
  EcsEntityReparentRequestedEvent(Engine::EntityHandle entity, Engine::EntityHandle parent)
      : m_Entity(entity), m_Parent(parent) {}

  static auto GetStaticType() -> Engine::Events::EventType {
    return Engine::Events::EventType::EcsEntityReparentRequested;
  }
  auto GetType() const -> Engine::Events::EventType override { return GetStaticType(); }
  auto GetEntity() const noexcept -> Engine::EntityHandle { return m_Entity; }
  auto GetParent() const noexcept -> Engine::EntityHandle { return m_Parent; }

private:
  Engine::EntityHandle m_Entity;
  Engine::EntityHandle m_Parent;
};

#pragma once

#include "runtime/include/events/Event.h"
#include "scene/include/SceneIds.h"

class EntityReparentRequestedEvent final : public Engine::Events::Event
{
  public:
    EntityReparentRequestedEvent(Engine::EntityId entityId, Engine::EntityId parentId)
        : m_EntityId(entityId), m_ParentId(parentId)
    {
    }
    static auto GetStaticType() -> Engine::Events::EventType
    {
        return Engine::Events::EventType::EntityReparentRequested;
    }
    auto GetType() const -> Engine::Events::EventType override
    {
        return GetStaticType();
    }
    auto GetEntityId() const noexcept -> Engine::EntityId
    {
        return m_EntityId;
    }
    auto GetParentId() const noexcept -> Engine::EntityId
    {
        return m_ParentId;
    }

  private:
    Engine::EntityId m_EntityId;
    Engine::EntityId m_ParentId;
};

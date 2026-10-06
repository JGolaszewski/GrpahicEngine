#pragma once

#include "Scene.h"
#include "events/Event.h"

class RemoveEntityRequestedEvent final : public Engine::Events::Event
{
  public:
    explicit RemoveEntityRequestedEvent(Engine::EntityId id) : m_Id(id)
    {
    }
    static Engine::Events::EventType GetStaticType()
    {
        return Engine::Events::EventType::RemoveEntityRequested;
    }
    Engine::Events::EventType GetType() const override
    {
        return GetStaticType();
    }
    Engine::EntityId GetId() const noexcept
    {
        return m_Id;
    }

  private:
    Engine::EntityId m_Id;
};

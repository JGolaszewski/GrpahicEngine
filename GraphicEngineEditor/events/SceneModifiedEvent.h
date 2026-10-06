#pragma once

#include "events/Event.h"

class SceneModifiedEvent final : public Engine::Events::Event
{
  public:
    static Engine::Events::EventType GetStaticType()
    {
        return Engine::Events::EventType::SceneModified;
    }
    Engine::Events::EventType GetType() const override
    {
        return GetStaticType();
    }
};

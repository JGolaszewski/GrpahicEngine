#pragma once

#include "events/Event.h"

class SceneFileActionRequestedEvent final : public Engine::Events::Event
{
  public:
    enum class Action
    {
        NewScene,
        OpenScene,
        SaveScene,
        SaveSceneAs,
        OpenProject
    };

    explicit SceneFileActionRequestedEvent(Action action) : m_Action(action)
    {
    }
    static Engine::Events::EventType GetStaticType()
    {
        return Engine::Events::EventType::SceneFileActionRequested;
    }
    Engine::Events::EventType GetType() const override
    {
        return GetStaticType();
    }
    Action GetAction() const noexcept
    {
        return m_Action;
    }

  private:
    Action m_Action;
};

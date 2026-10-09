#pragma once

#include "scene/include/Scene.h"
#include "runtime/include/events/Event.h"
#include <vector>

// ================================================================================
// SceneChangedEvent.h
// ================================================================================
// Broadcast by the Editor whenever the scene composition changes (e.g. an
// object is added or removed). Panels use this to update their internal
// entity lists.
// ================================================================================

class SceneChangedEvent : public Engine::Events::Event
{
  public:
    SceneChangedEvent(std::vector<Engine::EntityId> objectIds, std::vector<Engine::LightId> lightIds,
                      std::vector<Engine::CameraId> cameraIds)
        : m_ObjectIds(std::move(objectIds)), m_LightIds(std::move(lightIds)),
          m_CameraIds(std::move(cameraIds))
    {
    }

    static Engine::Events::EventType GetStaticType()
    {
        return Engine::Events::EventType::SceneChanged;
    }

    Engine::Events::EventType GetType() const override
    {
        return GetStaticType();
    }

    [[nodiscard]] const std::vector<Engine::EntityId> &GetObjectIds() const noexcept
    {
        return m_ObjectIds;
    }
    [[nodiscard]] const std::vector<Engine::LightId> &GetLightIds() const noexcept
    {
        return m_LightIds;
    }
    [[nodiscard]] const std::vector<Engine::CameraId> &GetCameraIds() const noexcept
    {
        return m_CameraIds;
    }

  private:
    std::vector<Engine::EntityId> m_ObjectIds;
    std::vector<Engine::LightId> m_LightIds;
    std::vector<Engine::CameraId> m_CameraIds;
};

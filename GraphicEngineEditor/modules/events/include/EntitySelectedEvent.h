#pragma once

#include "scene/include/Scene.h"
#include "runtime/include/events/Event.h"
#include <memory>

namespace Engine::Renderer
{
class Entity;
class SpotLight;
class Camera;
} // namespace Engine::Renderer

// ================================================================================
// EntitySelectedEvent.h
// ================================================================================
// Broadcast by the Editor when the selected entity changes. All panels
// receive this event and update their internal state accordingly.
// ================================================================================

class EntitySelectedEvent : public Engine::Events::Event
{
  public:
    enum class ItemType
    {
        None,
        Object,
        Light,
        Camera
    };

    EntitySelectedEvent(ItemType type, int objectIndex, int lightIndex, int cameraIndex,
                        Engine::EntityId entityId, Engine::LightId lightId, Engine::CameraId cameraId,
                        std::weak_ptr<::Engine::Renderer::Entity> entity = {},
                        std::weak_ptr<::Engine::Renderer::SpotLight> light = {},
                        std::weak_ptr<::Engine::Renderer::Camera> camera = {})
        : m_Type(type), m_ObjectIndex(objectIndex), m_LightIndex(lightIndex), m_CameraIndex(cameraIndex),
          m_EntityId(entityId), m_LightId(lightId), m_CameraId(cameraId), m_Entity(std::move(entity)),
          m_Light(std::move(light)), m_Camera(std::move(camera))
    {
    }

    static Engine::Events::EventType GetStaticType()
    {
        return Engine::Events::EventType::EntitySelected;
    }

    Engine::Events::EventType GetType() const override
    {
        return GetStaticType();
    }

    [[nodiscard]] ItemType GetItemType() const noexcept
    {
        return m_Type;
    }
    [[nodiscard]] int GetObjectIndex() const noexcept
    {
        return m_ObjectIndex;
    }
    [[nodiscard]] int GetLightIndex() const noexcept
    {
        return m_LightIndex;
    }
    [[nodiscard]] int GetCameraIndex() const noexcept
    {
        return m_CameraIndex;
    }

    [[nodiscard]] Engine::EntityId GetEntityId() const noexcept
    {
        return m_EntityId;
    }
    [[nodiscard]] Engine::LightId GetLightId() const noexcept
    {
        return m_LightId;
    }
    [[nodiscard]] Engine::CameraId GetCameraId() const noexcept
    {
        return m_CameraId;
    }

    [[nodiscard]] std::weak_ptr<::Engine::Renderer::Entity> GetEntity() const noexcept
    {
        return m_Entity;
    }
    [[nodiscard]] std::weak_ptr<::Engine::Renderer::SpotLight> GetLight() const noexcept
    {
        return m_Light;
    }
    [[nodiscard]] std::weak_ptr<::Engine::Renderer::Camera> GetCamera() const noexcept
    {
        return m_Camera;
    }

  private:
    ItemType m_Type;
    int m_ObjectIndex;
    int m_LightIndex;
    int m_CameraIndex;

    Engine::EntityId m_EntityId;
    Engine::LightId m_LightId;
    Engine::CameraId m_CameraId;

    std::weak_ptr<::Engine::Renderer::Entity> m_Entity;
    std::weak_ptr<::Engine::Renderer::SpotLight> m_Light;
    std::weak_ptr<::Engine::Renderer::Camera> m_Camera;
};

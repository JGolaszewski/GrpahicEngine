#pragma once

#include "events/Event.h"
#include <memory>

namespace Renderer {
    class Entity;
    class SpotLight;
    class Camera;
}

// ================================================================================
// EntitySelectedEvent.h
// ================================================================================
// Broadcast by the Editor when the selected entity changes. All panels
// receive this event and update their internal state accordingly.
// ================================================================================

class EntitySelectedEvent : public Engine::Events::Event {
public:
    enum class ItemType { None, Object, Light, Camera };

    EntitySelectedEvent(ItemType type,
                        int objectIndex,
                        int lightIndex,
                        int cameraIndex,
                        Renderer::Entity* entity,
                        Renderer::SpotLight* light,
                        std::shared_ptr<Renderer::Camera> camera)
        : m_Type(type)
        , m_ObjectIndex(objectIndex)
        , m_LightIndex(lightIndex)
        , m_CameraIndex(cameraIndex)
        , m_Entity(entity)
        , m_Light(light)
        , m_Camera(std::move(camera))
    {}

    static Engine::Events::EventType GetStaticType() {
        return Engine::Events::EventType::EntitySelected;
    }

    Engine::Events::EventType GetType() const override {
        return GetStaticType();
    }

    [[nodiscard]] ItemType GetItemType() const noexcept { return m_Type; }
    [[nodiscard]] int GetObjectIndex() const noexcept { return m_ObjectIndex; }
    [[nodiscard]] int GetLightIndex() const noexcept { return m_LightIndex; }
    [[nodiscard]] int GetCameraIndex() const noexcept { return m_CameraIndex; }

    [[nodiscard]] Renderer::Entity* GetEntity() const noexcept { return m_Entity; }
    [[nodiscard]] Renderer::SpotLight* GetLight() const noexcept { return m_Light; }
    [[nodiscard]] std::shared_ptr<Renderer::Camera> GetCamera() const noexcept { return m_Camera; }

private:
    ItemType m_Type;
    int      m_ObjectIndex;
    int      m_LightIndex;
    int      m_CameraIndex;

    Renderer::Entity*  m_Entity;  // Non-owning
    Renderer::SpotLight* m_Light; // Non-owning
    std::shared_ptr<Renderer::Camera> m_Camera;
};

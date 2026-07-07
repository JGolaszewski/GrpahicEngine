#pragma once

#include "events/Event.h"
#include "renderer/Material.h"

namespace Renderer {
    class Entity;
}

// ================================================================================
// MaterialChangedEvent.h
// ================================================================================
// Emitted by the InspectorPanel when the user edits material properties.
// The Editor receives this, opens a command list, and applies the properties
// to the entity's material.
// ================================================================================

class MaterialChangedEvent : public Engine::Events::Event {
public:
    MaterialChangedEvent(Renderer::Entity* entity, const Renderer::MaterialProperties& properties)
        : m_Entity(entity)
        , m_Properties(properties)
    {}

    static Engine::Events::EventType GetStaticType() {
        return Engine::Events::EventType::MaterialChanged;
    }

    Engine::Events::EventType GetType() const override {
        return GetStaticType();
    }

    [[nodiscard]] Renderer::Entity* GetEntity() const noexcept { return m_Entity; }
    [[nodiscard]] const Renderer::MaterialProperties& GetProperties() const noexcept { return m_Properties; }

private:
    Renderer::Entity* m_Entity;             // Non-owning
    Renderer::MaterialProperties m_Properties;
};

#pragma once

#include "events/Event.h"
#include "Scene.h"
#include "renderer/Material.h"
#include <cstdint>

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
    MaterialChangedEvent(Engine::EntityId entityId, const Renderer::MaterialProperties& properties, int32_t materialSlot = -1)
        : m_EntityId(entityId)
        , m_Properties(properties)
        , m_MaterialSlot(materialSlot)
    {}

    static Engine::Events::EventType GetStaticType() {
        return Engine::Events::EventType::MaterialChanged;
    }

    Engine::Events::EventType GetType() const override {
        return GetStaticType();
    }

    [[nodiscard]] Engine::EntityId GetEntityId() const noexcept { return m_EntityId; }
    [[nodiscard]] const Renderer::MaterialProperties& GetProperties() const noexcept { return m_Properties; }
    [[nodiscard]] int32_t GetMaterialSlot() const noexcept { return m_MaterialSlot; }

private:
    Engine::EntityId m_EntityId;
    Renderer::MaterialProperties m_Properties;
    int32_t m_MaterialSlot = -1;
};

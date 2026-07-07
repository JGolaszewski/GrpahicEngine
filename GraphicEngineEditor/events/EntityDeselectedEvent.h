#pragma once

#include "events/Event.h"

// ================================================================================
// EntityDeselectedEvent.h
// ================================================================================
// Broadcast by the Editor (or emitted by a panel) when the current selection
// is cleared.
// ================================================================================

class EntityDeselectedEvent : public Engine::Events::Event {
public:
    static Engine::Events::EventType GetStaticType() {
        return Engine::Events::EventType::EntityDeselected;
    }

    Engine::Events::EventType GetType() const override {
        return GetStaticType();
    }
};

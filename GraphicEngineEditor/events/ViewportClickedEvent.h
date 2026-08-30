#pragma once

#include "events/Event.h"

class ViewportClickedEvent : public Engine::Events::Event {
public:
    ViewportClickedEvent(float x, float y)
        : m_X(x)
        , m_Y(y) {
    }

    static Engine::Events::EventType GetStaticType() {
        return Engine::Events::EventType::ViewportClicked;
    }

    Engine::Events::EventType GetType() const override {
        return GetStaticType();
    }

    [[nodiscard]] float GetX() const noexcept { return m_X; }
    [[nodiscard]] float GetY() const noexcept { return m_Y; }

private:
    float m_X = 0.0f;
    float m_Y = 0.0f;
};

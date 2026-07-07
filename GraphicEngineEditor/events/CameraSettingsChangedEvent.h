#pragma once

#include "events/Event.h"

// ================================================================================
// CameraSettingsChangedEvent.h
// ================================================================================
// Emitted by the InspectorPanel when the user changes FOV, near plane, or
// far plane in the camera inspector. The Editor receives this and triggers
// UpdateProjection().
// ================================================================================

class CameraSettingsChangedEvent : public Engine::Events::Event {
public:
    CameraSettingsChangedEvent(float fov, float nearPlane, float farPlane)
        : m_Fov(fov)
        , m_NearPlane(nearPlane)
        , m_FarPlane(farPlane)
    {}

    static Engine::Events::EventType GetStaticType() {
        return Engine::Events::EventType::CameraSettingsChanged;
    }

    Engine::Events::EventType GetType() const override {
        return GetStaticType();
    }

    [[nodiscard]] float GetFov() const noexcept { return m_Fov; }
    [[nodiscard]] float GetNearPlane() const noexcept { return m_NearPlane; }
    [[nodiscard]] float GetFarPlane() const noexcept { return m_FarPlane; }

private:
    float m_Fov;
    float m_NearPlane;
    float m_FarPlane;
};

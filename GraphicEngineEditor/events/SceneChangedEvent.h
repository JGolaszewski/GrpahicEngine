#pragma once

#include "events/Event.h"
#include <cstddef>

// ================================================================================
// SceneChangedEvent.h
// ================================================================================
// Broadcast by the Editor whenever the scene composition changes (e.g. an
// object is added or removed). Panels use this to update their internal
// entity lists.
// ================================================================================

class SceneChangedEvent : public Engine::Events::Event {
public:
    SceneChangedEvent(size_t objectCount, size_t lightCount, size_t cameraCount)
        : m_ObjectCount(objectCount)
        , m_LightCount(lightCount)
        , m_CameraCount(cameraCount)
    {}

    static Engine::Events::EventType GetStaticType() {
        return Engine::Events::EventType::SceneChanged;
    }

    Engine::Events::EventType GetType() const override {
        return GetStaticType();
    }

    [[nodiscard]] size_t GetObjectCount() const noexcept { return m_ObjectCount; }
    [[nodiscard]] size_t GetLightCount() const noexcept { return m_LightCount; }
    [[nodiscard]] size_t GetCameraCount() const noexcept { return m_CameraCount; }

private:
    size_t m_ObjectCount;
    size_t m_LightCount;
    size_t m_CameraCount;
};

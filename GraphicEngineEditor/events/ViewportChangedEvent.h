#pragma once

#include "events/Event.h"

// ================================================================================
// ViewportChangedEvent.h
// ================================================================================
// Broadcast by the Editor after the viewport framebuffer has been recreated.
// Carries the new texture handle and dimensions so the ScenePanel can
// display the updated image.
// ================================================================================

class ViewportChangedEvent : public Engine::Events::Event {
public:
    ViewportChangedEvent(void* textureId, float width, float height)
        : m_TextureId(textureId)
        , m_Width(width)
        , m_Height(height)
    {}

    static Engine::Events::EventType GetStaticType() {
        return Engine::Events::EventType::ViewportChanged;
    }

    Engine::Events::EventType GetType() const override {
        return GetStaticType();
    }

    [[nodiscard]] void*  GetTextureId() const noexcept { return m_TextureId; }
    [[nodiscard]] float GetWidth() const noexcept { return m_Width; }
    [[nodiscard]] float GetHeight() const noexcept { return m_Height; }

private:
    void*  m_TextureId;
    float  m_Width;
    float  m_Height;
};

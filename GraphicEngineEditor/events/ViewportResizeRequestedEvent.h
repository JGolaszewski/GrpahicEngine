#pragma once

#include "events/Event.h"
#include <cstdint>

// ================================================================================
// ViewportResizeRequestedEvent.h
// ================================================================================
// Emitted by the ScenePanel when the ImGui viewport window changes size.
// The Editor receives this, recreates the framebuffer, and then broadcasts
// a ViewportChangedEvent in response.
// ================================================================================

class ViewportResizeRequestedEvent : public Engine::Events::Event
{
  public:
    ViewportResizeRequestedEvent(uint32_t width, uint32_t height) : m_Width(width), m_Height(height)
    {
    }

    static Engine::Events::EventType GetStaticType()
    {
        return Engine::Events::EventType::ViewportResizeRequested;
    }

    Engine::Events::EventType GetType() const override
    {
        return GetStaticType();
    }

    [[nodiscard]] uint32_t GetWidth() const noexcept
    {
        return m_Width;
    }
    [[nodiscard]] uint32_t GetHeight() const noexcept
    {
        return m_Height;
    }

  private:
    uint32_t m_Width;
    uint32_t m_Height;
};

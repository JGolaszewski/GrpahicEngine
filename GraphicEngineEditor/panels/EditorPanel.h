#pragma once

#include "events/Event.h"
#include <functional>
#include <memory>

namespace Engine::Events
{
class Event;
}

// ================================================================================
// EditorPanel.h
// ================================================================================
// Base class for all editor panels. Provides the Draw() contract, an
// OnEvent() hook for top-down event propagation, and an EventEmitter
// callback so panels can communicate with the Editor without holding a
// direct pointer to it.
// ================================================================================

class EditorPanel
{
  public:
    using EventEmitter = std::function<void(std::unique_ptr<Engine::Events::Event>)>;

    virtual ~EditorPanel() = default;

    // Called every frame to render the panel's ImGui content.
    virtual void Draw() = 0;

    // Called during the top-down event propagation phase.
    // If a panel consumes an input event, it must set event.Handled = true.
    virtual void OnEvent(Engine::Events::Event &event)
    {
    }

    // Set by the Editor after construction. Panels use EmitEvent() to send
    // state-change requests back to the Editor.
    void SetEventEmitter(EventEmitter emitter)
    {
        m_Emitter = std::move(emitter);
    }

  protected:
    // Emits an event to the Editor. The Editor receives it synchronously
    // and is responsible for updating global state and broadcasting back.
    void EmitEvent(std::unique_ptr<Engine::Events::Event> event)
    {
        if (m_Emitter)
        {
            m_Emitter(std::move(event));
        }
    }

  private:
    EventEmitter m_Emitter;
};

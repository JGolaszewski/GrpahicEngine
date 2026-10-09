#pragma once

#include "runtime/include/events/Event.h"
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
    using RequestHandler = std::function<void(std::unique_ptr<Engine::Events::Event>)>;

    virtual ~EditorPanel() = default;

    // Called every frame to render the panel's ImGui content.
    virtual void Draw() = 0;

    // Called during the top-down event propagation phase.
    // If a panel consumes an input event, it must set event.Handled = true.
    virtual void OnEvent(Engine::Events::Event &)
    {
    }

    // Set by the Editor after construction. Panels use EmitEvent() to send
    // state-change requests back to the Editor.
  protected:
    // Emits an event to the Editor. The Editor receives it synchronously
    // and is responsible for updating global state and broadcasting back.
    void EmitEvent(std::unique_ptr<Engine::Events::Event> event)
    {
        if (m_RequestSink)
            m_RequestSink(std::move(event));
    }

  private:
    void SetRequestSink(RequestHandler requestSink)
    {
        m_RequestSink = std::move(requestSink);
    }

    RequestHandler m_RequestSink;

    friend class EditorEventRouter;
};

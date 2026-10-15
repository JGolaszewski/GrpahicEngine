#pragma once

#include "runtime/include/CoreExport.h"
#include "runtime/include/events/Event.h"

#include <stdint.h>

namespace Engine
{

/// Platform-neutral window interface consumed by the application and render backend.
class ENGINE_API IWindow
{
  public:
    /// Releases platform window resources.
    virtual ~IWindow() = default;

    /// Returns the native platform window handle as an opaque pointer.
    /// @return Platform window handle for backend initialization.
    virtual void *GetNativeHandle() = 0;

    /// Returns the handle used by the engine's event-source integrations.
    /// @return Event-source handle associated with this window.
    virtual void *GetEventSourceHandle() const = 0;

    /// Reports whether the user or platform requested window closure.
    /// @return True when the window should stop receiving frames.
    virtual bool ShouldClose() const = 0;

    /// Polls the platform event queue and dispatches pending callbacks.
    virtual void PollEvents() = 0;

    /// Replaces the callback invoked for translated engine events.
    /// @param callback Event callback to install.
    virtual void SetEventCallback(Events::EventCallbackFn callback) = 0;

    /// Shows or hides the operating-system cursor.
    /// @param visible True to show the cursor.
    virtual void SetCursorVisible(bool visible) = 0;

    /// Returns the current framebuffer width in pixels.
    /// @return Current framebuffer width.
    virtual uint32_t GetWidth() const = 0;

    /// Returns the current framebuffer height in pixels.
    /// @return Current framebuffer height.
    virtual uint32_t GetHeight() const = 0;
};

} // namespace Engine

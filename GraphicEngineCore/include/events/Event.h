#pragma once
#pragma once

#include "CoreExport.h"

#include <functional>

namespace Engine::Events {
  enum class KeyCode {
        Unknown = 0,
        W,
        A,
        S,
        D,
        Q,
        E,
        Z,
        Y,
        N,
        O,
        Space,
        LeftControl,
        LeftShift,
        Delete,
        Count
    };

    enum class MouseButton {
        Unknown = 0,
        Left,
        Right,
        Middle,
        Button4,
        Button5,
        Count
    };

    enum class EventType {
        None = 0,
        KeyPressed,
        KeyReleased,
        MouseButtonPressed,
        MouseButtonReleased,
        MouseMoved,
        MouseScrolled,
        // Editor events
        EntitySelected,
        EntityDeselected,
        SceneChanged,
        ViewportChanged,
        ViewportClicked,
        ViewportHovered,
        ViewportResizeRequested,
        CameraSettingsChanged,
        MaterialChanged,
        AddObjectRequested,
        CommandExecuteRequested,
        DebugRenderToggled,
        SceneFileActionRequested,
        AssetImportRequested,
        AssetRegistryChanged,
        AddModelRequested,
        RemoveEntityRequested,
        SceneModified
    };

    class ENGINE_API Event {
    public:
        virtual ~Event() = default;
        virtual EventType GetType() const = 0;

        bool Handled = false;
    };

    class ENGINE_API EventDispatcher {
    public:
        explicit EventDispatcher(Event& event)
            : m_Event(event) {
        }

        template <typename TEvent, typename TFunction>
        bool Dispatch(const TFunction& callback) {
            if(m_Event.GetType() != TEvent::GetStaticType()) {
                return false;
            }

            m_Event.Handled = m_Event.Handled || callback(static_cast<TEvent&>(m_Event));
            return true;
        }

    private:
        Event& m_Event;
    };

    class ENGINE_API KeyPressedEvent : public Event {
    public:
       KeyPressedEvent(KeyCode key, int scancode, int mods, bool repeat)
            : Key(key), Scancode(scancode), Mods(mods), IsRepeat(repeat) {
        }

        static EventType GetStaticType() {
            return EventType::KeyPressed;
        }

        EventType GetType() const override {
            return GetStaticType();
        }

        KeyCode Key;
        int Scancode;
        int Mods;
        bool IsRepeat;
    };

    class ENGINE_API KeyReleasedEvent : public Event {
    public:
       KeyReleasedEvent(KeyCode key, int scancode, int mods)
            : Key(key), Scancode(scancode), Mods(mods) {
        }

        static EventType GetStaticType() {
            return EventType::KeyReleased;
        }

        EventType GetType() const override {
            return GetStaticType();
        }

        KeyCode Key;
        int Scancode;
        int Mods;
    };

    class ENGINE_API MouseButtonPressedEvent : public Event {
    public:
       MouseButtonPressedEvent(MouseButton button, int mods)
            : Button(button), Mods(mods) {
        }

        static EventType GetStaticType() {
            return EventType::MouseButtonPressed;
        }

        EventType GetType() const override {
            return GetStaticType();
        }

     MouseButton Button;
        int Mods;
    };

    class ENGINE_API MouseButtonReleasedEvent : public Event {
    public:
      MouseButtonReleasedEvent(MouseButton button, int mods)
            : Button(button), Mods(mods) {
        }

        static EventType GetStaticType() {
            return EventType::MouseButtonReleased;
        }

        EventType GetType() const override {
            return GetStaticType();
        }

     MouseButton Button;
        int Mods;
    };

    class ENGINE_API MouseMovedEvent : public Event {
    public:
        MouseMovedEvent(double x, double y, double deltaX, double deltaY)
            : X(x), Y(y), DeltaX(deltaX), DeltaY(deltaY) {
        }

        static EventType GetStaticType() {
            return EventType::MouseMoved;
        }

        EventType GetType() const override {
            return GetStaticType();
        }

        double X;
        double Y;
        double DeltaX;
        double DeltaY;
    };

    class ENGINE_API MouseScrolledEvent : public Event {
    public:
        MouseScrolledEvent(double xOffset, double yOffset)
            : XOffset(xOffset), YOffset(yOffset) {
        }

        static EventType GetStaticType() {
            return EventType::MouseScrolled;
        }

        EventType GetType() const override {
            return GetStaticType();
        }

        double XOffset;
        double YOffset;
    };

    using EventCallbackFn = std::function<void(Event&)>;
}

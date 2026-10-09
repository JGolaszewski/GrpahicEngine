#pragma once
#pragma once

#include "runtime/include/CoreExport.h"
#include "runtime/include/Window.h"

struct GLFWwindow;

namespace Engine
{

class GLFWWindow : public IWindow
{
  public:
    GLFWWindow(int width, int height, const char *title);
    ~GLFWWindow() override;

    void *GetNativeHandle() override;
    void *GetEventSourceHandle() const override;

    bool ShouldClose() const override;
    void PollEvents() override;
    void SetEventCallback(Events::EventCallbackFn callback) override;
    void SetCursorVisible(bool visible) override;

    uint32_t GetWidth() const override;
    uint32_t GetHeight() const override;

  private:
    void SetupEventCallbacks();
    static Events::KeyCode MapKeyCode(int key);
    static Events::MouseButton MapMouseButton(int button);

    GLFWwindow *m_Window = nullptr;
    Events::EventCallbackFn m_EventCallback;
    double m_LastMouseX = 0.0;
    double m_LastMouseY = 0.0;
    bool m_HasMousePosition = false;
};
} // namespace Engine

#include "windows/GLFWWindow.h"
#include "runtime/include/LogEngine.h"
#include "runtime/include/LogMacros.h"

#include <GLFW/glfw3.h>
#include <cstdint>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace Engine {

auto GLFWWindow::MapKeyCode(int key) -> Events::KeyCode {
  switch (key) {
  case GLFW_KEY_W:
    return Events::KeyCode::W;
  case GLFW_KEY_A:
    return Events::KeyCode::A;
  case GLFW_KEY_S:
    return Events::KeyCode::S;
  case GLFW_KEY_D:
    return Events::KeyCode::D;
  case GLFW_KEY_Q:
    return Events::KeyCode::Q;
  case GLFW_KEY_E:
    return Events::KeyCode::E;
  case GLFW_KEY_Z:
    return Events::KeyCode::Z;
  case GLFW_KEY_Y:
    return Events::KeyCode::Y;
  case GLFW_KEY_N:
    return Events::KeyCode::N;
  case GLFW_KEY_O:
    return Events::KeyCode::O;
  case GLFW_KEY_SPACE:
    return Events::KeyCode::Space;
  case GLFW_KEY_LEFT_CONTROL:
    return Events::KeyCode::LeftControl;
  case GLFW_KEY_LEFT_SHIFT:
    return Events::KeyCode::LeftShift;
  case GLFW_KEY_DELETE:
    return Events::KeyCode::Delete;
  default:
    return Events::KeyCode::Unknown;
  }
}

auto GLFWWindow::MapMouseButton(int button) -> Events::MouseButton {
  switch (button) {
  case GLFW_MOUSE_BUTTON_LEFT:
    return Events::MouseButton::Left;
  case GLFW_MOUSE_BUTTON_RIGHT:
    return Events::MouseButton::Right;
  case GLFW_MOUSE_BUTTON_MIDDLE:
    return Events::MouseButton::Middle;
  case GLFW_MOUSE_BUTTON_4:
    return Events::MouseButton::Button4;
  case GLFW_MOUSE_BUTTON_5:
    return Events::MouseButton::Button5;
  default:
    return Events::MouseButton::Unknown;
  }
}

GLFWWindow::GLFWWindow(int width, int height, const char* title) {
#if ENGINE_ENABLE_LOGGING
  if (!Log::Initialize()) {
    const Log::HealthSnapshot health = Log::GetHealthSnapshot();
    ENGINE_LOG_ERROR_CODE("Diagnostics", "logging.initialize.sink_failed",
                          "Logger initialized with an unavailable file sink | path: {} | error: {}",
                          health.jsonlPath.generic_string(), health.lastSinkError);
  }
#endif
  ENGINE_LOG_INFO("Core", "GLFW window initialization started | width: {} | height: {} | title: {}", width, height,
                  title ? title : "<null>");
  if (glfwInit() == 0) {
    ENGINE_LOG_ERROR("Core", "GLFW initialization failed | width: {} | height: {}", width, height);
    return;
  }

  // No OpenGL context
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  m_Window = glfwCreateWindow(width, height, title, nullptr, nullptr);

  if (m_Window == nullptr) {
    ENGINE_LOG_ERROR("Core", "GLFW window creation failed | width: {} | height: {} | title: {}", width, height,
                     title ? title : "<null>");
    glfwTerminate();
    return;
  }

  glfwSetWindowUserPointer(m_Window, this);
  SetupEventCallbacks();
  ENGINE_LOG_INFO("Core", "GLFW window initialized | native_handle: {} | width: {} | height: {}",
                  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                  reinterpret_cast<uintptr_t>(glfwGetWin32Window(m_Window)), width, height);
}

GLFWWindow::~GLFWWindow() {
  if (m_Window != nullptr) {
    glfwDestroyWindow(m_Window);
  }
  glfwTerminate();
}

auto Engine::GLFWWindow::GetWidth() const -> uint32_t {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(m_Window, &width, &height);

  return static_cast<uint32_t>(width);
}

auto Engine::GLFWWindow::GetHeight() const -> uint32_t {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(m_Window, &width, &height);

  return static_cast<uint32_t>(height);
}

auto GLFWWindow::GetNativeHandle() -> void* {
  if (m_Window == nullptr) {
    return nullptr;
  }

  return static_cast<void*>(glfwGetWin32Window(m_Window));
}

auto GLFWWindow::GetEventSourceHandle() const -> void* { return static_cast<void*>(m_Window); }

auto GLFWWindow::ShouldClose() const -> bool {
  return ((m_Window != nullptr) ? glfwWindowShouldClose(m_Window) : 1) != 0;
}

void GLFWWindow::PollEvents() { glfwPollEvents(); }

void GLFWWindow::SetEventCallback(Events::EventCallbackFn callback) { m_EventCallback = std::move(callback); }

void GLFWWindow::SetCursorVisible(bool visible) {
  if (m_Window == nullptr) {
    return;
  }

  glfwSetInputMode(m_Window, GLFW_CURSOR, visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

void GLFWWindow::SetupEventCallbacks() {
  if (m_Window == nullptr) {
    return;
  }

  glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods) -> void {
    auto* self = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
    if (!self || !self->m_EventCallback) {
      return;
    }

    const auto mappedKey = GLFWWindow::MapKeyCode(key);

    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
      Events::KeyPressedEvent event(mappedKey, scancode, mods, action == GLFW_REPEAT);
      self->m_EventCallback(event);
    } else if (action == GLFW_RELEASE) {
      Events::KeyReleasedEvent event(mappedKey, scancode, mods);
      self->m_EventCallback(event);
    }
  });

  glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods) -> void {
    auto* self = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
    if (!self || !self->m_EventCallback) {
      return;
    }

    const auto mappedButton = GLFWWindow::MapMouseButton(button);

    if (action == GLFW_PRESS) {
      Events::MouseButtonPressedEvent event(mappedButton, mods);
      self->m_EventCallback(event);
    } else if (action == GLFW_RELEASE) {
      Events::MouseButtonReleasedEvent event(mappedButton, mods);
      self->m_EventCallback(event);
    }
  });

  glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xpos, double ypos) -> void {
    auto* self = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
    if (!self || !self->m_EventCallback) {
      return;
    }

    double deltaX = 0.0;
    double deltaY = 0.0;
    if (self->m_HasMousePosition) {
      deltaX = xpos - self->m_LastMouseX;
      deltaY = ypos - self->m_LastMouseY;
    }

    self->m_LastMouseX = xpos;
    self->m_LastMouseY = ypos;
    self->m_HasMousePosition = true;

    Events::MouseMovedEvent event(xpos, ypos, deltaX, deltaY);
    self->m_EventCallback(event);
  });

  glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xoffset, double yoffset) -> void {
    auto* self = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
    if (!self || !self->m_EventCallback) {
      return;
    }

    Events::MouseScrolledEvent event(xoffset, yoffset);
    self->m_EventCallback(event);
  });
}

} // namespace Engine

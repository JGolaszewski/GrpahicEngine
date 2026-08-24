#include "windows/GLFWWindow.h"
#include "windows/GLFWWindow.h"

#include <iostream>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace Engine {

	Events::KeyCode GLFWWindow::MapKeyCode(int key) {
		switch(key) {
		case GLFW_KEY_W: return Events::KeyCode::W;
		case GLFW_KEY_A: return Events::KeyCode::A;
		case GLFW_KEY_S: return Events::KeyCode::S;
		case GLFW_KEY_D: return Events::KeyCode::D;
		case GLFW_KEY_Q: return Events::KeyCode::Q;
		case GLFW_KEY_E: return Events::KeyCode::E;
		case GLFW_KEY_Z: return Events::KeyCode::Z;
		case GLFW_KEY_Y: return Events::KeyCode::Y;
		case GLFW_KEY_SPACE: return Events::KeyCode::Space;
		case GLFW_KEY_LEFT_CONTROL: return Events::KeyCode::LeftControl;
		case GLFW_KEY_LEFT_SHIFT: return Events::KeyCode::LeftShift;
		case GLFW_KEY_DELETE: return Events::KeyCode::Delete;
		default: return Events::KeyCode::Unknown;
		}
	}

	Events::MouseButton GLFWWindow::MapMouseButton(int button) {
		switch(button) {
		case GLFW_MOUSE_BUTTON_LEFT: return Events::MouseButton::Left;
		case GLFW_MOUSE_BUTTON_RIGHT: return Events::MouseButton::Right;
		case GLFW_MOUSE_BUTTON_MIDDLE: return Events::MouseButton::Middle;
		case GLFW_MOUSE_BUTTON_4: return Events::MouseButton::Button4;
		case GLFW_MOUSE_BUTTON_5: return Events::MouseButton::Button5;
		default: return Events::MouseButton::Unknown;
		}
	}

	GLFWWindow::GLFWWindow(int width, int height, const char* title) {
		if(!glfwInit()) {
			std::cerr << "ERROR: GLFW initialization failed." << std::endl;
			return;
		}

		// No OpenGL context
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

       glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

		m_Window = glfwCreateWindow(width, height, title, nullptr, nullptr);

		if(!m_Window) {
			std::cerr << "ERROR: Window could not be created." << std::endl;
			glfwTerminate();
           return;
		}

		glfwSetWindowUserPointer(m_Window, this);
		SetupEventCallbacks();
	}

	GLFWWindow::~GLFWWindow() {
		if(m_Window) {
			glfwDestroyWindow(m_Window);
		}
		glfwTerminate();
	}

	uint32_t Engine::GLFWWindow::GetWidth() const {
		int width, height;
		glfwGetFramebufferSize(m_Window, &width, &height);

		return static_cast<uint32_t>(width);
	}

	uint32_t Engine::GLFWWindow::GetHeight() const {
		int width, height;
		glfwGetFramebufferSize(m_Window, &width, &height);

		return static_cast<uint32_t>(height);
	}

	void* GLFWWindow::GetNativeHandle() {
		if(!m_Window) return nullptr;

		return static_cast<void*>(glfwGetWin32Window(m_Window));
	}

	bool GLFWWindow::ShouldClose() const {
		return m_Window ? glfwWindowShouldClose(m_Window) : true;
	}

	void GLFWWindow::PollEvents() {
		glfwPollEvents();
	}

	void GLFWWindow::SetEventCallback(Events::EventCallbackFn callback) {
		m_EventCallback = std::move(callback);
	}

	void GLFWWindow::SetCursorVisible(bool visible) {
		if(!m_Window) {
			return;
		}

		glfwSetInputMode(m_Window, GLFW_CURSOR, visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
	}

	void GLFWWindow::SetupEventCallbacks() {
		if(!m_Window) {
			return;
		}

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
			auto* self = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
			if(!self || !self->m_EventCallback) {
				return;
			}

         const auto mappedKey = GLFWWindow::MapKeyCode(key);

			if(action == GLFW_PRESS || action == GLFW_REPEAT) {
              Events::KeyPressedEvent event(mappedKey, scancode, mods, action == GLFW_REPEAT);
				self->m_EventCallback(event);
			} else if(action == GLFW_RELEASE) {
                Events::KeyReleasedEvent event(mappedKey, scancode, mods);
				self->m_EventCallback(event);
			}
		});

		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods) {
			auto* self = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
			if(!self || !self->m_EventCallback) {
				return;
			}

           const auto mappedButton = GLFWWindow::MapMouseButton(button);

			if(action == GLFW_PRESS) {
                Events::MouseButtonPressedEvent event(mappedButton, mods);
				self->m_EventCallback(event);
			} else if(action == GLFW_RELEASE) {
               Events::MouseButtonReleasedEvent event(mappedButton, mods);
				self->m_EventCallback(event);
			}
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xpos, double ypos) {
			auto* self = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
			if(!self || !self->m_EventCallback) {
				return;
			}

			double deltaX = 0.0;
			double deltaY = 0.0;
			if(self->m_HasMousePosition) {
				deltaX = xpos - self->m_LastMouseX;
				deltaY = ypos - self->m_LastMouseY;
			}

			self->m_LastMouseX = xpos;
			self->m_LastMouseY = ypos;
			self->m_HasMousePosition = true;

			Events::MouseMovedEvent event(xpos, ypos, deltaX, deltaY);
			self->m_EventCallback(event);
		});

		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xoffset, double yoffset) {
			auto* self = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
			if(!self || !self->m_EventCallback) {
				return;
			}

			Events::MouseScrolledEvent event(xoffset, yoffset);
			self->m_EventCallback(event);
		});
	}

	void GLFWWindow::InitImGui() {
		ImGui_ImplGlfw_InitForOther(m_Window, true);

		float xScale = 1.0f;
		float yScale = 1.0f;
		glfwGetWindowContentScale(m_Window, &xScale, &yScale);

		const float uiScale = xScale > yScale ? xScale : yScale;
		if(uiScale > 1.0f) {
			ImGuiStyle& style = ImGui::GetStyle();
			style.ScaleAllSizes(uiScale);
			ImGui::GetIO().FontGlobalScale = uiScale;
		}
	}

	void GLFWWindow::ImGuiNewFrame() {
		ImGui_ImplGlfw_NewFrame();
	}

	void GLFWWindow::ShutdownImGui() {
		ImGui_ImplGlfw_Shutdown();
	}

}

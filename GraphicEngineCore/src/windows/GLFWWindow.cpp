#include "windows/GLFWWindow.h"

#include <iostream>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace Engine {

	GLFWWindow::GLFWWindow(int width, int height, const char* title) {
		if(!glfwInit()) {
			std::cerr << "ERROR: GLFW initialization failed." << std::endl;
			return;
		}

		// No OpenGL context
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		// TODO: Block resize for now
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		m_Window = glfwCreateWindow(width, height, title, nullptr, nullptr);

		if(!m_Window) {
			std::cerr << "ERROR: Window could not be created." << std::endl;
			glfwTerminate();
		}
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

	void GLFWWindow::InitImGui() {
		ImGui_ImplGlfw_InitForOther(m_Window, true);
	}

	void GLFWWindow::ImGuiNewFrame() {
		ImGui_ImplGlfw_NewFrame();
	}

}
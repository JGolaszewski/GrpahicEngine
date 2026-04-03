#pragma once

#include "CoreExport.h"
#include "Window.h"

struct GLFWwindow;

namespace Engine {

	class ENGINE_API GLFWWindow : public IWindow {
	public:
		GLFWWindow(int width, int height, const char* title);
		~GLFWWindow() override;

		void* GetNativeHandle() override;

		bool ShouldClose() const override;
		void PollEvents() override;

		inline GLFWwindow* GetGLFWHandle() const {
			return m_Window;
		}

		void InitImGui() override;
		void ImGuiNewFrame() override;

		uint32_t GetWidth() const override;
		uint32_t GetHeight() const override;

	private:
		GLFWwindow* m_Window = nullptr;
	};
}
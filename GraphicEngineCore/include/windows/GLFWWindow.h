#pragma once
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
		void SetEventCallback(Events::EventCallbackFn callback) override;
		void SetCursorVisible(bool visible) override;

		void InitImGui() override;
		void ImGuiNewFrame() override;
		void ShutdownImGui() override;

		uint32_t GetWidth() const override;
		uint32_t GetHeight() const override;

	private:
        void SetupEventCallbacks();
		static Events::KeyCode MapKeyCode(int key);
		static Events::MouseButton MapMouseButton(int button);

		GLFWwindow* m_Window = nullptr;
      Events::EventCallbackFn m_EventCallback;
		double m_LastMouseX = 0.0;
		double m_LastMouseY = 0.0;
		bool m_HasMousePosition = false;
	};
}

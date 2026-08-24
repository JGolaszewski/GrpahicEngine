#include "Application.h"
#include "Window.h"
#include "RenderBackend.h"

#include "windows/GLFWWindow.h"
#include "backends/D3D11Backend.h"

#include <iostream>
#include <chrono>

#include <imgui.h>

namespace Engine {

    Application::Application() {}
    Application::~Application() {}

    bool Application::InitializeCore(RenderAPI api) {
        m_Window = std::make_unique<GLFWWindow>(1280, 720, "Editor");

        if(api == RenderAPI::D3D11) {
            m_RenderBackend = std::make_unique<D3D11Backend>();
        } else {
            std::cerr << "ERROR: Not supported API." << std::endl;
            return false;
        }

        if(!m_RenderBackend->Initialize(m_Window.get())) {
            std::cerr << "ERROR: Could not initialize renderer." << std::endl;
            return false;
        }
        
        m_RenderSystem = std::make_unique<RenderSystem>(m_RenderBackend.get());

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGui::StyleColorsDark();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        m_Window->InitImGui();
        m_RenderBackend->InitImGui(m_Window.get());
        m_Window->SetEventCallback([this](Events::Event& event) {
            OnEvent(event);
        });

        return true;
    }

	void Application::Run() {
		if (!InitializeCore(RenderAPI::D3D11)) {
			return;
		}

		m_IsRunning = true;
		OnInit();

		auto lastTime = std::chrono::high_resolution_clock::now();
		uint32_t lastWidth = m_Window->GetWidth();
		uint32_t lastHeight = m_Window->GetHeight();

		while (m_IsRunning && !m_Window->ShouldClose()) {

			auto currentTime = std::chrono::high_resolution_clock::now();
			float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
			lastTime = currentTime;

			m_Window->PollEvents();

			uint32_t currentWidth = m_Window->GetWidth();
			uint32_t currentHeight = m_Window->GetHeight();

			if (currentWidth > 0 && currentHeight > 0 && (currentWidth != lastWidth || currentHeight != lastHeight)) {
				m_RenderBackend->Resize(static_cast<int>(currentWidth), static_cast<int>(currentHeight));
				OnResize(currentWidth, currentHeight);
				lastWidth = currentWidth;
				lastHeight = currentHeight;
			}

			OnUpdate(deltaTime);

			ClearViews();

			OnSetupViews();

			m_RenderSystem->BeginFrame();

            m_RenderSystem->SubmitViews(m_ActiveViews);
			OnRender();

			m_RenderSystem->EndFrame();

			m_RenderBackend->BeginImGuiFrame();
			m_Window->ImGuiNewFrame();
			ImGui::NewFrame();

			OnUIRender();

			ImGui::Render();
			m_RenderBackend->RenderImGui();

			m_RenderBackend->Present();
		}
	}

    void Application::AddScene(std::shared_ptr<Scene> scene) {
        m_ActiveScenes.push_back(scene);
    }

    void Application::RemoveScene(std::shared_ptr<Scene> scene) {
        auto it = std::find(m_ActiveScenes.begin(), m_ActiveScenes.end(), scene);
        if (it != m_ActiveScenes.end()) {
            m_ActiveScenes.erase(it);
        }
    }

    void Application::AddViewport(std::shared_ptr<Engine::Viewport> viewport) {
        m_Viewports.push_back(viewport);
    }

    void Application::RemoveViewport(std::shared_ptr<Engine::Viewport> viewport) {
        auto it = std::find(m_Viewports.begin(), m_Viewports.end(), viewport);
        if (it != m_Viewports.end()) {
            m_Viewports.erase(it);
        }
    }


	void Application::RegisterView(const ViewportBinding& binding)
	{
		if (binding.IsValid()) {
			m_ActiveViews.push_back(binding);
		}
	}

	ImGuiContext* Application::GetImGuiContext() {
        return ImGui::GetCurrentContext();
    }

    IWindow* Application::GetWindow() const {
        return m_Window.get();
    }
}

#include "Application.h"
#include "Window.h"
#include "RenderBackend.h"

#include "windows/GLFWWindow.h"
#include "backends/D3D11Backend.h"
#include "renderer/Viewport.h"

#include <iostream>
#include <chrono>
#include <algorithm>
#include <thread>

#include <imgui.h>

namespace Engine {

    Application::Application() {}
    Application::~Application() {
        if (ImGui::GetCurrentContext()) {
            if (m_RenderBackend) {
                m_RenderBackend->ShutdownImGui();
            }
            if (m_Window) {
                m_Window->ShutdownImGui();
            }
            ImGui::DestroyContext();
        }

        m_ActiveViews.clear();
        m_ActiveScenes.clear();
        m_Viewports.clear();
        m_DefaultViewport.reset();
        if (m_RenderBackend && m_RenderBackend->GetDevice()) {
            m_RenderBackend->GetDevice()->waitForIdle();
        }
        m_RenderSystem.reset();
        m_RenderBackend.reset();
        m_Window.reset();
    }

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
		if (!m_RenderSystem->IsValid()) {
			std::cerr << "ERROR: Renderer command-list initialization failed." << std::endl;
			return false;
		}
		m_DefaultViewport = std::make_shared<Viewport>(
			m_Window->GetWidth(),
			m_Window->GetHeight(),
			m_RenderBackend->GetDefaultFramebuffer());

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
			const auto frameStart = std::chrono::steady_clock::now();

			auto currentTime = std::chrono::high_resolution_clock::now();
			float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
			lastTime = currentTime;
			m_LastFrameDeltaTime = deltaTime;
			m_FrameTimingStats.UnclampedDeltaTime = deltaTime;

			m_Window->PollEvents();

			uint32_t currentWidth = m_Window->GetWidth();
			uint32_t currentHeight = m_Window->GetHeight();

			if (currentWidth > 0 && currentHeight > 0 && (currentWidth != lastWidth || currentHeight != lastHeight)) {
				m_RenderBackend->Resize(static_cast<int>(currentWidth), static_cast<int>(currentHeight));
				m_DefaultViewport = std::make_shared<Viewport>(
					currentWidth, currentHeight, m_RenderBackend->GetDefaultFramebuffer());
				OnResize(currentWidth, currentHeight);
				lastWidth = currentWidth;
				lastHeight = currentHeight;
			}

			const float gameplayDeltaTime = (std::min)(deltaTime, 0.1f);
			m_FrameTimingStats.GameplayDeltaTime = gameplayDeltaTime;
			if (gameplayDeltaTime != deltaTime) {
				++m_FrameTimingStats.ClampedFrameCount;
			}
			OnUpdate(gameplayDeltaTime);
			UpdateScenes(gameplayDeltaTime);

			ClearViews();

			OnSetupViews();
			RegisterDefaultView();

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

			m_FrameTimingStats.CpuFrameTimeMs = std::chrono::duration<float, std::milli>(
				std::chrono::steady_clock::now() - frameStart).count();
			++m_FrameTimingStats.FrameCount;
			if (m_FrameLimiterHz > 0) {
				const auto frameDuration = std::chrono::duration<float>(1.0f / static_cast<float>(m_FrameLimiterHz));
				std::this_thread::sleep_until(frameStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameDuration));
			}
		}
	}

	void Application::UpdateScenes(float deltaTime) {
		for (const auto& scene : m_ActiveScenes) {
			if (scene) {
				scene->Update(deltaTime);
			}
		}
	}

	void Application::RegisterDefaultView() {
		if (!m_ActiveViews.empty() || !m_DefaultViewport) {
			return;
		}

		for (const auto& scene : m_ActiveScenes) {
			if (!scene) {
				continue;
			}

			Renderer::Camera* camera = scene->GetCamera(0);
			if (!camera) {
				continue;
			}

			RegisterView({ camera, m_DefaultViewport.get(), scene.get() });
			return;
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

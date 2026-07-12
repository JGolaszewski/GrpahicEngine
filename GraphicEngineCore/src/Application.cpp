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
        if(!InitializeCore(RenderAPI::D3D11)) {
            return;
        }

        m_IsRunning = true;
        OnInit();

        auto lastTime = std::chrono::high_resolution_clock::now();
        uint32_t lastWidth = m_Window->GetWidth();
        uint32_t lastHeight = m_Window->GetHeight();

        while(m_IsRunning && !m_Window->ShouldClose()) {

            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;

            m_Window->PollEvents();

            uint32_t currentWidth = m_Window->GetWidth();
            uint32_t currentHeight = m_Window->GetHeight();

            if(currentWidth > 0 && currentHeight > 0 && (currentWidth != lastWidth || currentHeight != lastHeight)) {
                m_RenderBackend->Resize(static_cast<int>(currentWidth), static_cast<int>(currentHeight));
                OnResize(currentWidth, currentHeight);
                lastWidth = currentWidth;
                lastHeight = currentHeight;
            }

            OnUpdate(deltaTime);
            // m_ActiveScene->Update(deltaTime);

            // TODO: Add Scene and Viewport
            m_RenderSystem->RenderFrame()

            OnRender();

            if(sceneFramebuffer && m_ActiveCamera) {
                auto viewProjMatrix = m_ActiveCamera->GetViewProjection();
                m_SceneRenderer->RenderAll(m_CommandList.Get(), sceneFramebuffer, viewProjMatrix);
            }

            m_Renderer->BeginImGuiFrame();
            m_Window->ImGuiNewFrame();
            ImGui::NewFrame();

            OnUIRender();

            m_CommandList->close();
            nvrhiDevice->executeCommandList(m_CommandList);

            ImGui::Render();
            m_Renderer->RenderImGui();

            m_Renderer->Present();
        }
    }

    ImGuiContext* Application::GetImGuiContext() {
        return ImGui::GetCurrentContext();
    }

    IWindow* Application::GetWindow() const {
        return m_Window.get();
    }
}

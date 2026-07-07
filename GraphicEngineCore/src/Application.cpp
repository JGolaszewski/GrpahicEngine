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
            m_Renderer = std::make_unique<D3D11Backend>();
        } else {
            std::cerr << "ERROR: Not supported API." << std::endl;
            return false;
        }

        if(!m_Renderer->Initialize(m_Window.get())) {
            std::cerr << "ERROR: Could not initialize renderer." << std::endl;
            return false;
        }

        nvrhi::IDevice* device = m_Renderer->GetDevice();
        if(!device) return false;

        nvrhi::CommandListParameters cmdParams;
        m_CommandList = device->createCommandList(cmdParams);

        m_ShaderFactory = std::make_unique<Renderer::ShaderFactory>(m_Renderer->GetDevice());

        m_SceneRenderer = std::make_unique<SceneRenderer>();
        m_SceneRenderer->Init(device);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGui::StyleColorsDark();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        m_Window->InitImGui();
        m_Renderer->InitImGui(m_Window.get());
        m_Window->SetEventCallback([this](Events::Event& event) {
            OnEvent(event);
        });

        return true;
    }

    static void ClearFramebuffer(nvrhi::ICommandList* cmd, nvrhi::IFramebuffer* framebuffer) {
        if(!framebuffer) {
            return;
        }

        nvrhi::ITexture* colorTex = framebuffer->getDesc().colorAttachments[0].texture;
        if(colorTex) {
            cmd->clearTextureFloat(colorTex, nvrhi::AllSubresources, nvrhi::Color(0.1f, 0.1f, 0.1f, 1.0f));
        }

        nvrhi::ITexture* depthTex = framebuffer->getDesc().depthAttachment.texture;
        if(depthTex) {
            cmd->clearDepthStencilTexture(depthTex, nvrhi::AllSubresources, true, 1.0f, false, 0);
        }
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
                m_Renderer->Resize(static_cast<int>(currentWidth), static_cast<int>(currentHeight));
                OnResize(currentWidth, currentHeight);
                lastWidth = currentWidth;
                lastHeight = currentHeight;
            }

            OnUpdate(deltaTime);

            nvrhi::IDevice* nvrhiDevice = m_Renderer->GetDevice();
            nvrhi::IFramebuffer* sceneFramebuffer = GetSceneRenderFramebuffer();
            nvrhi::IFramebuffer* defaultFramebuffer = m_Renderer->GetDefaultFramebuffer();

            m_CommandList->open();

            ClearFramebuffer(m_CommandList.Get(), sceneFramebuffer);
            ClearFramebuffer(m_CommandList.Get(), defaultFramebuffer);

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

    SceneRenderer* Application::GetSceneRenderer() const {
        return m_SceneRenderer.get();
    }

    void Application::SetActiveCamera(std::shared_ptr<Renderer::Camera> camera) {
        m_ActiveCamera = camera;
    }

    std::shared_ptr<Renderer::Camera> Application::GetActiveCamera() const {
        return m_ActiveCamera;
    }

    ImGuiContext* Application::GetImGuiContext() {
        return ImGui::GetCurrentContext();
    }

    Renderer::ShaderFactory* Application::GetShaderFactory() {
        return m_ShaderFactory.get();
    }

    Engine::IRenderBackend* Application::GetRenderer() const {
        return m_Renderer.get();
    }

    nvrhi::ICommandList* Application::GetCommandList() const {
        return m_CommandList.Get();
    }

    IWindow* Application::GetWindow() const {
        return m_Window.get();
    }

}

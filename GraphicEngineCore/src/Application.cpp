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
		m_Window = std::make_unique<GLFWWindow>(1280, 720, "APPLICATION");

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

		m_Window->InitImGui();
		m_Renderer->InitImGui(m_Window.get());

		return true;
	}

	void Application::Run() {
		if(!InitializeCore(RenderAPI::D3D11)) {
			return;
		}

		m_IsRunning = true;
		OnInit();

		auto lastTime = std::chrono::high_resolution_clock::now();

		while(m_IsRunning && !m_Window->ShouldClose()) {

			auto currentTime = std::chrono::high_resolution_clock::now();
			float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
			lastTime = currentTime;

			m_Window->PollEvents();
			OnUpdate(deltaTime);

			nvrhi::IDevice* nvrhiDevice = m_Renderer->GetDevice();
			nvrhi::IFramebuffer* framebuffer = m_Renderer->GetDefaultFramebuffer();

			m_CommandList->open();

			if(framebuffer) {
				nvrhi::ITexture* backBufferTex = framebuffer->getDesc().colorAttachments[0].texture;
				nvrhi::Color clearColor = nvrhi::Color(0.1f, 0.1f, 0.1f, 1.0f);
				m_CommandList->clearTextureFloat(backBufferTex, nvrhi::AllSubresources, clearColor);
			}

			OnRender();

			if(framebuffer) {
				auto viewProjMatrix = m_ActiveCamera->GetViewProjection();
				m_SceneRenderer->RenderAll(m_CommandList.Get(), framebuffer, viewProjMatrix);
			}

			m_Renderer->BeginImGuiFrame();
			m_Window->ImGuiNewFrame();
			ImGui::NewFrame();

			OnUIRender();

			ImGui::Render();
			m_Renderer->RenderImGui();

			m_CommandList->close();
			nvrhiDevice->executeCommandList(m_CommandList);

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

}
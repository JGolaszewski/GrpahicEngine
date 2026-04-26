#pragma once

#include "CoreExport.h"
#include "Window.h"
#include "RenderBackend.h"
#include "renderer/ShaderFactory.h"

#include "SceneRenderer.h" 
#include "renderer/Camera.h"

#include <memory>
#include <DirectXMath.h> 

struct ImGuiContext;

namespace Engine {

	enum class RenderAPI {
		D3D11
	};

	class ENGINE_API Application {
	public:
		Application();
		virtual ~Application();

		void Run();
		ImGuiContext* GetImGuiContext();
		Renderer::ShaderFactory* GetShaderFactory();
		IRenderBackend* GetRenderer() const;
		nvrhi::ICommandList* GetCommandList() const;
		IWindow* GetWindow() const;

		SceneRenderer* GetSceneRenderer() const;
		void SetActiveCamera(std::shared_ptr<Renderer::Camera> camera);
		std::shared_ptr<Renderer::Camera> GetActiveCamera() const;
	protected:
		virtual void OnInit() {};
		virtual void OnUpdate(float deltaTime) {};
		virtual void OnRender() {};
		virtual void OnUIRender() {};
		virtual void OnResize(uint32_t width, uint32_t height) {};
		virtual void OnEvent(Events::Event& event) {};

	private:
		bool InitializeCore(RenderAPI api);

		std::unique_ptr<IWindow> m_Window;
		std::unique_ptr<IRenderBackend> m_Renderer;
		std::unique_ptr<Renderer::ShaderFactory> m_ShaderFactory;

		std::unique_ptr<SceneRenderer> m_SceneRenderer;

		std::shared_ptr<Renderer::Camera> m_ActiveCamera;

		nvrhi::CommandListHandle m_CommandList;

		bool m_IsRunning = false;
	};
}
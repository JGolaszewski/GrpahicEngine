#pragma once

#include "CoreExport.h"
#include "Window.h"
#include "RenderBackend.h"
#include "renderer/ShaderFactory.h"
#include "RenderSystem.h"

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

		void AddScene(std::shared_ptr<Scene> scene);
		void RemoveScene(std::shared_ptr<Scene> scene);

		void AddViewport(std::shared_ptr<Renderer::Viewport> viewport);
		void RemoveViewport(std::shared_ptr<Renderer::Viewport> viewport);

        ImGuiContext* GetImGuiContext();
        IWindow* GetWindow() const;

    protected:
        virtual void OnInit() {};
        virtual void OnUpdate(float deltaTime) {};
        virtual void OnRender() {};
        virtual void OnUIRender() {};
        virtual void OnResize(uint32_t width, uint32_t height) {};
        virtual void OnEvent(Events::Event& event) {};

    private:
        bool InitializeCore(RenderAPI api);

        bool m_IsRunning = false;

        std::unique_ptr<IWindow> m_Window;
        std::unique_ptr<IRenderBackend> m_RenderBackend;
		std::unique_ptr<RenderSystem> m_RenderSystem;

		std::vector<std::shared_ptr<Scene>> m_ActiveScenes;
		std::vector<std::shared_ptr<Renderer::Viewport>> m_Viewports;
    };
}

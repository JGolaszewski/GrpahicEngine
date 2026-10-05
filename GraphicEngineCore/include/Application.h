#pragma once

#include "CoreExport.h"
#include "Window.h"
#include "RenderBackend.h"
#include "RenderSystem.h"
#include "AssetManager.h"
#include "SceneDocument.h"

#include "renderer/ViewportBinding.h"

#include <memory>
#include <vector>
#include <functional>
#include <cstdint>

struct ImGuiContext;

namespace Engine {

    enum class RenderAPI {
        D3D11
    };

    class ENGINE_API Application {
    public:
		struct FrameTimingStats {
			float CpuFrameTimeMs = 0.0f;
			float UnclampedDeltaTime = 0.0f;
			float GameplayDeltaTime = 0.0f;
			uint64_t FrameCount = 0;
			uint64_t ClampedFrameCount = 0;
		};

        Application();
        virtual ~Application();

        void Run();

        void AddScene(std::shared_ptr<Scene> scene);
        void RemoveScene(std::shared_ptr<Scene> scene);
		bool LoadScene(const std::filesystem::path& path, std::string& error, std::vector<std::string>& warnings);
		bool SaveScene(const std::filesystem::path& path, const Scene& scene, std::string& error) const;

        void AddViewport(std::shared_ptr<Engine::Viewport> viewport);
        void RemoveViewport(std::shared_ptr<Engine::Viewport> viewport);

        void RegisterView(const ViewportBinding& binding);

        ImGuiContext* GetImGuiContext();
        IWindow* GetWindow() const;
        float GetLastFrameDeltaTime() const noexcept { return m_LastFrameDeltaTime; }
		const FrameTimingStats& GetFrameTimingStats() const noexcept { return m_FrameTimingStats; }
		void SetFrameLimiterHz(uint32_t hz) noexcept { m_FrameLimiterHz = hz; }

        // --- Renderer helpers used by Editor and panels ---
        IRenderBackend* GetRenderer() const { return m_RenderBackend.get(); }
        RenderSystem* GetRenderSystem() const { return m_RenderSystem.get(); }
        AssetManager* GetAssetManager() noexcept { return &m_AssetManager; }
        const AssetManager* GetAssetManager() const noexcept { return &m_AssetManager; }
        SceneRenderer* GetSceneRenderer() const { return m_RenderSystem ? m_RenderSystem->GetSceneRenderer() : nullptr; }
        Renderer::ShaderFactory* GetShaderFactory() const { return m_RenderSystem ? m_RenderSystem->GetShaderFactory() : nullptr; }

        void ExecuteImmediate(std::function<void(nvrhi::ICommandList*)> fn) {
            if (m_RenderSystem) {
                m_RenderSystem->ExecuteImmediate(std::move(fn));
            }
        }

        const std::vector<std::shared_ptr<Scene>>& GetScenes() const { return m_ActiveScenes; }
        const std::vector<std::shared_ptr<Engine::Viewport>>& GetViewports() const { return m_Viewports; }

        void ClearViews() { m_ActiveViews.clear(); }
    protected:
        virtual void OnInit() {}
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnRender() {}
        virtual void OnSetupViews() {}
        virtual void OnUIRender() {}
        virtual void OnResize(uint32_t width, uint32_t height) {}
        virtual void OnEvent(Events::Event& event) {}
		virtual void OnSceneReplaced(const std::shared_ptr<Scene>&) {}

    private:
        bool InitializeCore(RenderAPI api);
        void RegisterDefaultView();
		void UpdateScenes(float deltaTime);

        bool m_IsRunning = false;
		float m_LastFrameDeltaTime = 0.0f;
		uint32_t m_FrameLimiterHz = 0;
		FrameTimingStats m_FrameTimingStats;

        std::unique_ptr<IWindow> m_Window;
        std::unique_ptr<IRenderBackend> m_RenderBackend;
        std::unique_ptr<RenderSystem> m_RenderSystem;
        AssetManager m_AssetManager;

        std::vector<std::shared_ptr<Scene>> m_ActiveScenes;
        std::vector<std::shared_ptr<Engine::Viewport>> m_Viewports;
		std::shared_ptr<Engine::Viewport> m_DefaultViewport;
        
		std::vector<ViewportBinding> m_ActiveViews;
    };
}

#pragma once

#include "rendering/include/renderer/ViewportBinding.h"
#include "runtime/include/CoreExport.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace nvrhi {
class ICommandList;
}

namespace Engine::Events {
class Event;
}

namespace Engine::Renderer {
class ShaderFactory;
}

namespace Engine {
class AssetManager;
class IRenderBackend;
class IWindow;
class RenderSystem;
class Scene;
class SceneRenderer;
struct SceneDocument;
class Viewport;

class ENGINE_API Application {
public:
  struct FrameTimingStats {
    float CpuFrameTimeMs = 0.0f;
    float FrameTimeMs = 0.0f;
    float UpdateTimeMs = 0.0f;
    float ViewSetupTimeMs = 0.0f;
    float RenderTimeMs = 0.0f;
    float UiTimeMs = 0.0f;
    float PresentTimeMs = 0.0f;
    float LimiterWaitMs = 0.0f;
    float GpuFrameTimeMs = 0.0f;
    float UnclampedDeltaTime = 0.0f;
    float GameplayDeltaTime = 0.0f;
    uint64_t FrameCount = 0;
    uint64_t ClampedFrameCount = 0;
    uint64_t GpuTimingFrameIndex = 0;
    bool GpuTimingAvailable = false;
    bool GpuTimingPending = false;
    uint32_t ObjectsSubmitted = 0;
    uint32_t ObjectsCulled = 0;
    uint32_t PacketsBuilt = 0;
    uint32_t OpaquePackets = 0;
    uint32_t MaskedPackets = 0;
    uint32_t TransparentPackets = 0;
    uint32_t PipelineCacheHits = 0;
    uint32_t PipelineCacheMisses = 0;
    uint32_t PipelineCreations = 0;
    uint32_t ViewsSubmitted = 0;
    uint32_t DrawCalls = 0;
    uint32_t BufferAllocations = 0;
    uint64_t UploadBytes = 0;
    float CullingTimeMs = 0.0f;
    float PacketBuildTimeMs = 0.0f;
  };

  static constexpr size_t FrameMetricsHistoryCapacity = 8192;

  struct FrameMetricsSample {
    uint64_t FrameIndex = 0;
    uint64_t TimestampNs = 0;
    FrameTimingStats Timing;
  };

  Application();
  virtual ~Application();

  void Run(std::unique_ptr<IWindow> window, std::unique_ptr<IRenderBackend> backend);

  void AddScene(const std::shared_ptr<Scene>& scene);
  void RemoveScene(const std::shared_ptr<Scene>& scene);
  bool LoadScene(const std::filesystem::path& path, std::string& error, std::vector<std::string>& warnings);
  bool SaveScene(const std::filesystem::path& path, const Scene& scene, std::string& error) const;
  bool LoadSceneDocument(const SceneDocument& document, std::string& error, std::vector<std::string>& warnings);
  static SceneDocument CreateSceneDocument(const Scene& scene);

  void AddViewport(const std::shared_ptr<Engine::Viewport>& viewport);
  void RemoveViewport(const std::shared_ptr<Engine::Viewport>& viewport);

  void RegisterView(const ViewportBinding& binding);

  IWindow* GetWindow() const;
  float GetLastFrameDeltaTime() const noexcept { return m_LastFrameDeltaTime; }
  const FrameTimingStats& GetFrameTimingStats() const noexcept { return m_FrameTimingStats; }
  void SetFrameLimiterHz(uint32_t hz) noexcept { m_FrameLimiterHz = hz; }
  uint32_t GetFrameLimiterHz() const noexcept { return m_FrameLimiterHz; }
  const std::array<FrameMetricsSample, FrameMetricsHistoryCapacity>& GetFrameMetricsHistory() const noexcept {
    return *m_FrameMetricsHistory;
  }
  size_t GetFrameMetricsCount() const noexcept { return m_FrameMetricsCount; }
  void SetSimulationPaused(bool paused) noexcept { m_SimulationPaused.store(paused, std::memory_order_release); }
  bool IsSimulationPaused() const noexcept { return m_SimulationPaused.load(std::memory_order_acquire); }

  // --- Renderer helpers used by Editor and panels ---
  IRenderBackend* GetRenderer() const { return m_RenderBackend.get(); }
  RenderSystem* GetRenderSystem() const { return m_RenderSystem.get(); }
  AssetManager* GetAssetManager() noexcept { return m_AssetManager.get(); }
  const AssetManager* GetAssetManager() const noexcept { return m_AssetManager.get(); }
  SceneRenderer* GetSceneRenderer() const;
  ::Engine::Renderer::ShaderFactory* GetShaderFactory() const;
  void ExecuteImmediate(const std::function<void(nvrhi::ICommandList*)>& fn);

  const std::vector<std::shared_ptr<Scene>>& GetScenes() const { return m_ActiveScenes; }
  const std::vector<std::shared_ptr<Engine::Viewport>>& GetViewports() const { return m_Viewports; }

  void ClearViews() { m_ActiveViews.clear(); }

protected:
  virtual void OnInit() {}
  virtual void OnUpdate(float) {}
  virtual void OnRender() {}
  virtual void OnSetupViews() {}
  virtual void OnUIRender() {}
  virtual void OnBeginUIFrame() {}
  virtual void OnEndUIFrame() {}
  virtual void OnShutdownUI() {}
  virtual void OnResize(uint32_t, uint32_t) {}
  virtual void OnEvent(Events::Event&) {}
  virtual void OnSceneReplaced(const std::shared_ptr<Scene>&) {}

private:
  bool InitializeCore();
  void RegisterDefaultView();
  void UpdateScenes(float deltaTime);

  bool m_IsRunning = false;
  std::atomic<bool> m_SimulationPaused{false};
  float m_LastFrameDeltaTime = 0.0f;
  uint32_t m_FrameLimiterHz = 0;
  FrameTimingStats m_FrameTimingStats;
  std::unique_ptr<std::array<FrameMetricsSample, FrameMetricsHistoryCapacity>> m_FrameMetricsHistory;
  size_t m_FrameMetricsCount = 0;

  std::unique_ptr<IWindow> m_Window;
  std::unique_ptr<IRenderBackend> m_RenderBackend;
  std::unique_ptr<RenderSystem> m_RenderSystem;
  std::unique_ptr<AssetManager> m_AssetManager;

  std::vector<std::shared_ptr<Scene>> m_ActiveScenes;
  std::vector<std::shared_ptr<Engine::Viewport>> m_Viewports;
  std::shared_ptr<Engine::Viewport> m_DefaultViewport;

  std::vector<ViewportBinding> m_ActiveViews;
};
} // namespace Engine

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

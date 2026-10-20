#pragma once

#include "rendering/include/renderer/ViewportBinding.h"
#include "runtime/include/CoreExport.h"
#include "runtime/include/Result.h"

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

namespace Engine::Events {
class Event;
}

namespace Engine {
class AssetManager;
class IRenderBackend;
class IWindow;
class RenderSystem;
class Scene;
struct SceneDocument;
class Viewport;

/// Owns the application window, renderer, active scenes, and frame loop.
class ENGINE_API Application {
public:
  /// Timing and submission counters collected for the most recently completed frame.
  struct FrameTimingStats {
    float CpuFrameTimeMs = 0.0f;      ///< CPU duration for the frame, excluding limiter wait.
    float FrameTimeMs = 0.0f;         ///< CPU frame duration including limiter wait.
    float UpdateTimeMs = 0.0f;        ///< Time spent in application and scene update.
    float ViewSetupTimeMs = 0.0f;     ///< Time spent configuring submitted views.
    float RenderTimeMs = 0.0f;        ///< Time spent submitting and executing render work.
    float UiTimeMs = 0.0f;            ///< Time spent building and ending the UI frame.
    float PresentTimeMs = 0.0f;       ///< Time spent presenting the back buffer.
    float LimiterWaitMs = 0.0f;       ///< Time spent waiting for the configured frame limit.
    float GpuFrameTimeMs = 0.0f;      ///< GPU duration for the reported completed frame, when available.
    float UnclampedDeltaTime = 0.0f;  ///< Wall-clock delta before gameplay clamping.
    float GameplayDeltaTime = 0.0f;   ///< Delta passed to update after the 100 ms cap.
    uint64_t FrameCount = 0;          ///< Number of completed frames.
    uint64_t ClampedFrameCount = 0;   ///< Number of frames whose gameplay delta was capped.
    uint64_t GpuTimingFrameIndex = 0; ///< Frame index associated with GpuFrameTimeMs.
    bool GpuTimingAvailable = false;  ///< Whether GPU timing data is valid for this sample.
    bool GpuTimingPending = false;    ///< Whether a GPU timing query is still pending.
    uint32_t ObjectsSubmitted = 0;    ///< Scene objects sent to visibility processing.
    uint32_t ObjectsCulled = 0;       ///< Scene objects rejected by visibility tests.
    uint32_t PacketsBuilt = 0;        ///< Total render packets built for this frame.
    uint32_t OpaquePackets = 0;       ///< Opaque packets built for this frame.
    uint32_t MaskedPackets = 0;       ///< Alpha-masked packets built for this frame.
    uint32_t TransparentPackets = 0;  ///< Transparent packets built for this frame.
    uint32_t PipelineCacheHits = 0;   ///< Pipeline lookups satisfied from the cache.
    uint32_t PipelineCacheMisses = 0; ///< Pipeline lookups that required creation.
    uint32_t PipelineCreations = 0;   ///< Graphics pipelines created during the frame.
    uint32_t ViewsSubmitted = 0;      ///< Views submitted to the renderer.
    uint32_t DrawCalls = 0;           ///< Draw calls recorded in the frame diagnostics.
    uint32_t BufferAllocations = 0;   ///< GPU buffer allocations made during the frame.
    uint64_t UploadBytes = 0;         ///< Bytes uploaded to GPU buffers during the frame.
    float CullingTimeMs = 0.0f;       ///< CPU time spent culling scene objects.
    float PacketBuildTimeMs = 0.0f;   ///< CPU time spent building render packets.
  };

  /// Maximum number of per-frame samples retained in the history ring.
  static constexpr size_t FrameMetricsHistoryCapacity = 8192;

  /// One timestamped entry in the rolling frame-metrics history.
  struct FrameMetricsSample {
    uint64_t FrameIndex = 0;  ///< Zero-based frame index.
    uint64_t TimestampNs = 0; ///< Monotonic timestamp in nanoseconds.
    FrameTimingStats Timing;  ///< Timing and submission counters for this frame.
  };

  /// Constructs an application with an empty scene list and no window or renderer.
  Application();

  /// Stops owned runtime services and releases their resources.
  virtual ~Application();

  /// Takes ownership of the window and backend, initializes the engine, and runs frames.
  /// @param window Window used for input, events, and presentation.
  /// @param backend Rendering backend paired with the window.
  void Run(std::unique_ptr<IWindow> window, std::unique_ptr<IRenderBackend> backend);

  /// Adds a scene to the update and serialization list.
  /// @param scene Scene to register; null entries are accepted and skipped during update.
  void AddScene(const std::shared_ptr<Scene>& scene);

  /// Removes the first matching scene from the active list.
  /// @param scene Scene to remove.
  void RemoveScene(const std::shared_ptr<Scene>& scene);

  /// Loads a scene file and replaces the active scene after successful validation.
  /// @param path Source scene document path.
  /// @param error Receives a diagnostic when loading or instantiation fails.
  /// @param warnings Receives non-fatal migration and asset warnings.
  /// @return True when the new scene is fully instantiated.
  bool LoadScene(const std::filesystem::path& path, std::string& error, std::vector<std::string>& warnings);

  /// Serializes a scene and atomically writes it to disk.
  /// @param path Destination scene document path.
  /// @param scene Scene to serialize.
  /// @param error Receives a diagnostic when saving fails.
  /// @return True when the file is saved successfully.
  bool SaveScene(const std::filesystem::path& path, const Scene& scene, std::string& error) const;

  /// Instantiates a validated document and makes its scene active.
  /// @param document Parsed scene data.
  /// @param error Receives a diagnostic when instantiation fails.
  /// @param warnings Receives non-fatal asset-resolution warnings.
  /// @return True when the document is instantiated successfully.
  bool LoadSceneDocument(const SceneDocument& document, std::string& error, std::vector<std::string>& warnings);

  /// Instantiates a scene document without registering or replacing any active scene.
  /// The returned scene owns independent ECS state; renderer and asset resources may be prepared or shared.
  /// @param document Parsed scene data to instantiate.
  /// @param warnings Receives non-fatal asset-resolution warnings.
  /// @return The detached scene, or an error when validation or instantiation fails.
  Result<std::shared_ptr<Scene>> CreateDetachedSceneFromDocument(const SceneDocument& document,
                                                                 std::vector<std::string>& warnings);

  /// Creates a serializable snapshot of one scene.
  /// @param scene Scene to snapshot.
  /// @return A document containing the scene's persistent data.
  SceneDocument CreateSceneDocument(const Scene& scene) const;

  /// Adds a viewport to the application-owned viewport list.
  /// @param viewport Viewport to register.
  void AddViewport(const std::shared_ptr<Engine::Viewport>& viewport);

  /// Removes the first matching viewport from the application-owned list.
  /// @param viewport Viewport to remove.
  void RemoveViewport(const std::shared_ptr<Engine::Viewport>& viewport);

  /// Queues a camera, viewport, and scene tuple for the next render frame.
  /// @param binding View submission to enqueue.
  void RegisterView(const ViewportBinding& binding);

  /// Returns the owned window, or null before Run initializes it.
  /// @return A borrowed window pointer, or null before initialization.
  IWindow* GetWindow() const;

  /// Returns the most recently measured frame delta in seconds.
  /// @return The latest raw frame delta in seconds.
  float GetLastFrameDeltaTime() const noexcept { return m_LastFrameDeltaTime; }
  /// Returns timing counters for the latest completed frame.
  /// @return A const reference to the latest frame statistics.
  const FrameTimingStats& GetFrameTimingStats() const noexcept { return m_FrameTimingStats; }
  /// Sets the target frame rate; zero disables frame limiting.
  /// @param hz Target frames per second, or zero to disable the limiter.
  void SetFrameLimiterHz(uint32_t hz) noexcept { m_FrameLimiterHz = hz; }
  /// Returns the configured frame-rate limit, or zero when disabled.
  /// @return Target frames per second, or zero when limiting is disabled.
  uint32_t GetFrameLimiterHz() const noexcept { return m_FrameLimiterHz; }
  /// Returns the fixed-capacity ring containing recent frame samples.
  /// @return A const reference to the frame sample ring.
  const std::array<FrameMetricsSample, FrameMetricsHistoryCapacity>& GetFrameMetricsHistory() const noexcept {
    return *m_FrameMetricsHistory;
  }
  /// Returns the number of valid entries currently stored in the history ring.
  /// @return Number of samples populated in the ring.
  size_t GetFrameMetricsCount() const noexcept { return m_FrameMetricsCount; }
  /// Pauses scene simulation updates while leaving rendering and UI active.
  /// @param paused True to pause simulation updates.
  void SetSimulationPaused(bool paused) noexcept { m_SimulationPaused.store(paused, std::memory_order_release); }
  /// Reports whether scene simulation updates are paused.
  /// @return True while scene simulation updates are paused.
  bool IsSimulationPaused() const noexcept { return m_SimulationPaused.load(std::memory_order_acquire); }

  /// Returns the backend borrowed by this application.
  /// @return A borrowed backend pointer, or null before Run initializes it.
  IRenderBackend* GetRenderer() const { return m_RenderBackend.get(); }
  /// Returns the render system borrowed by this application, or null before initialization.
  /// @return A borrowed render-system pointer, or null before initialization.
  RenderSystem* GetRenderSystem() const { return m_RenderSystem.get(); }
  /// Returns the mutable asset manager owned by this application.
  /// @return A borrowed pointer to the owned asset manager.
  AssetManager* GetAssetManager() noexcept { return m_AssetManager.get(); }
  /// Returns the const asset manager owned by this application.
  /// @return A borrowed const pointer to the owned asset manager.
  const AssetManager* GetAssetManager() const noexcept { return m_AssetManager.get(); }
  /// Returns the current scene list.
  /// @return A const reference to the registered scenes.
  const std::vector<std::shared_ptr<Scene>>& GetScenes() const { return m_ActiveScenes; }
  /// Returns the current viewport list.
  /// @return A const reference to the registered viewports.
  const std::vector<std::shared_ptr<Engine::Viewport>>& GetViewports() const { return m_Viewports; }

  /// Removes all view submissions queued for the current frame.
  void ClearViews() { m_ActiveViews.clear(); }

protected:
  /// Called once after the window and renderer initialize successfully.
  virtual void OnInit() {}
  /// Called once per frame with the clamped gameplay delta.
  /// @param deltaTime Frame delta in seconds after the gameplay clamp.
  virtual void OnUpdate(float deltaTime) { (void)deltaTime; }
  /// Called each frame after render views have been submitted.
  virtual void OnRender() {}
  /// Called before the default camera view is registered.
  virtual void OnSetupViews() {}
  /// Called to build the application's UI for the current frame.
  virtual void OnUIRender() {}
  /// Called before the application's UI widgets are rendered.
  virtual void OnBeginUIFrame() {}
  /// Called after the application's UI widgets are rendered.
  virtual void OnEndUIFrame() {}
  /// Called after the run loop exits so UI resources can be released.
  virtual void OnShutdownUI() {}
  /// Called when the framebuffer size changes to a nonzero size.
  /// @param width New framebuffer width in pixels.
  /// @param height New framebuffer height in pixels.
  virtual void OnResize(uint32_t width, uint32_t height) {
    (void)width;
    (void)height;
  }
  /// Called for each window event delivered during event polling.
  /// @param event Event being dispatched.
  virtual void OnEvent(Events::Event& event) { (void)event; }
  /// Called after a scene document replaces the active scene.
  /// @param scene Newly activated scene.
  virtual void OnSceneReplaced(const std::shared_ptr<Scene>& scene) { (void)scene; }

private:
  bool InitializeCore();
  void RegisterDefaultView();
  void UpdateScenes(float deltaTime);
  bool BuildSceneFromDocument(const SceneDocument& document, std::shared_ptr<Scene>& stagedScene, std::string& error,
                              std::vector<std::string>& warnings);

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

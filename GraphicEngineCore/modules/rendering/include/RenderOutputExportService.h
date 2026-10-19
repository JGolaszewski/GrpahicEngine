#pragma once

#include "rendering/include/RenderOutput.h"
#include "runtime/include/CoreExport.h"
#include "runtime/include/Result.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Engine {

class Scene;
class RenderSystem;
namespace Renderer {
struct CameraView;
}

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain standard-library state.
#endif

/// Current stage of an asynchronous image export.
enum class RenderOutputExportPhase : uint8_t { WaitingForReadback, Writing, Completed, Failed };

/// Snapshot of progress and user-facing result for an export request.
struct ENGINE_API RenderOutputExportStatus {
  uint64_t RequestId = 0;
  RenderOutputExportPhase Phase = RenderOutputExportPhase::WaitingForReadback;
  float Progress = 0.0F;
  std::filesystem::path Destination;
  std::string Message;
};

/// Submits final camera renders and writes PNG or selected AOV EXR files
/// asynchronously. Call Update and Shutdown on the render thread. Shutdown
/// must run before the associated RenderSystem and graphics device are destroyed.
class ENGINE_API RenderOutputExportService {
public:
  using RequestId = uint64_t;

  RenderOutputExportService();
  ~RenderOutputExportService() noexcept;
  RenderOutputExportService(const RenderOutputExportService&) = delete;
  RenderOutputExportService& operator=(const RenderOutputExportService&) = delete;

  /// Start a final-color PNG export. Rendering is submitted immediately; GPU
  /// completion and image writing are advanced by Update on later frames.
  /// An enabled background is composited into final_color by the writer thread.
  Engine::Result<RequestId> StartPng(RenderSystem& renderer, Scene& scene, const ::Engine::Renderer::CameraView& camera,
                                     uint32_t width, uint32_t height, std::filesystem::path destination,
                                     const FinalColorPostProcess& postProcess = {},
                                     const RenderOutputBackground& background = {});
  /// Start a multipart EXR export containing exactly the selected AOV planes.
  /// An enabled background is composited into final_color only when selected.
  Engine::Result<RequestId>
  StartMultilayerExr(RenderSystem& renderer, Scene& scene, const ::Engine::Renderer::CameraView& camera, uint32_t width,
                     uint32_t height, std::vector<RenderAov> selectedAovs, std::filesystem::path destination,
                     const FinalColorPostProcess& postProcess = {}, const RenderOutputBackground& background = {});

  /// Poll GPU readback and queue ready CPU-owned output for background writing.
  void Update();
  /// Return a copy of the current request state, or no value for an unknown id.
  std::optional<RenderOutputExportStatus> GetStatus(RequestId requestId) const;
  /// Cancel pending readback, finish queued file writes, and join the worker.
  void Shutdown();

private:
  struct Request {
    RenderOutputExportStatus Status;
    uint64_t RenderTicket = 0;
    bool IsExr = false;
    std::vector<RenderAov> SelectedAovs;
    RenderOutputBackground Background;
  };
  struct WriteTask {
    RequestId Id = 0;
    bool IsExr = false;
    std::vector<RenderAov> SelectedAovs;
    RenderOutputBackground Background;
    std::filesystem::path Destination;
    RenderOutput Output;
  };

  Engine::Result<RequestId> Start(RenderSystem& renderer, Engine::Scene& scene,
                                  const ::Engine::Renderer::CameraView& camera, uint32_t width, uint32_t height,
                                  bool exr, std::vector<RenderAov> selectedAovs, std::filesystem::path destination,
                                  const FinalColorPostProcess& postProcess, const RenderOutputBackground& background);
  void WorkerMain();

  mutable std::mutex m_Mutex;
  std::condition_variable m_WorkAvailable;
  std::deque<WriteTask> m_WriteQueue;
  std::vector<Request> m_Requests;
  std::optional<RequestId> m_ActiveReadback;
  RenderSystem* m_Renderer = nullptr;
  RequestId m_NextRequestId = 0;
  bool m_ShuttingDown = false;
  std::thread m_Worker;
};

} // namespace Engine

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include "rendering/include/RenderOutputExportService.h"

#include "rendering/include/RenderOutputExporter.h"
#include "rendering/include/RenderSystem.h"
#include "scene/include/Scene.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace Engine {

RenderOutputExportService::RenderOutputExportService() : m_Worker([this] { WorkerMain(); }) {}

RenderOutputExportService::~RenderOutputExportService() noexcept {
  try {
    Shutdown();
  } catch (...) {
    // The worker captures this service; destruction cannot safely continue if it cannot be joined.
    std::terminate();
  }
}

auto RenderOutputExportService::StartPng(RenderSystem& renderer, Scene& scene,
                                         const ::Engine::Renderer::CameraView& camera, uint32_t width, uint32_t height,
                                         std::filesystem::path destination, const FinalColorPostProcess& postProcess,
                                         const RenderOutputBackground& background) -> Engine::Result<RequestId> {
  return Start(renderer, scene, camera, width, height, false, {}, std::move(destination), postProcess, background);
}

auto RenderOutputExportService::StartMultilayerExr(
    RenderSystem& renderer, Scene& scene, const ::Engine::Renderer::CameraView& camera, uint32_t width, uint32_t height,
    std::vector<RenderAov> selectedAovs, std::filesystem::path destination, const FinalColorPostProcess& postProcess,
    const RenderOutputBackground& background) -> Engine::Result<RequestId> {
  return Start(renderer, scene, camera, width, height, true, std::move(selectedAovs), std::move(destination),
               postProcess, background);
}

auto RenderOutputExportService::Start(RenderSystem& renderer, Scene& scene,
                                      const ::Engine::Renderer::CameraView& camera, uint32_t width, uint32_t height,
                                      bool exr, std::vector<RenderAov> selectedAovs, std::filesystem::path destination,
                                      const FinalColorPostProcess& postProcess,
                                      const RenderOutputBackground& background) -> Engine::Result<RequestId> {
  {
    std::lock_guard lock(m_Mutex);
    if (m_ShuttingDown) {
      return std::unexpected(Engine::MakeError("export service has been shut down"));
    }
    if (m_ActiveReadback.has_value()) {
      return std::unexpected(Engine::MakeError("another export is waiting for GPU readback"));
    }
  }

  auto renderTicket = renderer.BeginFinalRender(scene, camera, width, height, postProcess);
  if (!renderTicket) {
    return std::unexpected(renderTicket.error());
  }

  std::lock_guard lock(m_Mutex);
  if (m_ShuttingDown || m_ActiveReadback.has_value()) {
    const Engine::Status cancellationStatus = renderer.CancelFinalRender(*renderTicket);
    if (!cancellationStatus) {
      return std::unexpected(Engine::MakeError("export request was rejected and render cancellation failed: " +
                                               cancellationStatus.error().Message));
    }
    return std::unexpected(Engine::MakeError("export service cannot accept another readback request"));
  }
  Request request;
  request.Status.RequestId = ++m_NextRequestId;
  if (request.Status.RequestId == 0) {
    request.Status.RequestId = ++m_NextRequestId;
  }
  request.Status.Phase = RenderOutputExportPhase::WaitingForReadback;
  request.Status.Progress = 0.0F;
  request.Status.Destination = std::move(destination);
  request.RenderTicket = *renderTicket;
  request.IsExr = exr;
  request.SelectedAovs = std::move(selectedAovs);
  request.Background = background;
  const RequestId id = request.Status.RequestId;
  m_Requests.push_back(std::move(request));
  m_ActiveReadback = id;
  m_Renderer = &renderer;
  return id;
}

void RenderOutputExportService::Update() {
  RenderSystem* renderer = nullptr;
  RequestId requestId = 0;
  RenderSystem::FinalRenderTicket renderTicket = 0;
  {
    std::lock_guard lock(m_Mutex);
    if (!m_ActiveReadback.has_value() || m_Renderer == nullptr || m_ShuttingDown) {
      return;
    }
    requestId = *m_ActiveReadback;
    renderer = m_Renderer;
    const auto request = std::ranges::find_if(
        m_Requests, [requestId](const Request& item) { return item.Status.RequestId == requestId; });
    if (request == m_Requests.end()) {
      m_ActiveReadback.reset();
      return;
    }
    renderTicket = request->RenderTicket;
  }

  auto result = renderer->PollFinalRender(renderTicket);
  if (!result) {
    std::lock_guard lock(m_Mutex);
    const auto request = std::ranges::find_if(
        m_Requests, [requestId](const Request& item) { return item.Status.RequestId == requestId; });
    if (request != m_Requests.end()) {
      request->Status.Phase = RenderOutputExportPhase::Failed;
      request->Status.Message = "Render or GPU readback failed: " + result.error().Message;
    }
    m_ActiveReadback.reset();
    return;
  }
  if (!result->has_value()) {
    return;
  }

  std::lock_guard lock(m_Mutex);
  const auto request =
      std::ranges::find_if(m_Requests, [requestId](const Request& item) { return item.Status.RequestId == requestId; });
  if (request == m_Requests.end()) {
    m_ActiveReadback.reset();
    return;
  }
  request->Status.Phase = RenderOutputExportPhase::Writing;
  request->Status.Progress = 0.5F;
  m_WriteQueue.push_back({.Id = requestId,
                          .IsExr = request->IsExr,
                          .SelectedAovs = std::move(request->SelectedAovs),
                          .Background = request->Background,
                          .Destination = request->Status.Destination,
                          .Output = std::move(result->value())});
  m_ActiveReadback.reset();
  m_WorkAvailable.notify_one();
}

auto RenderOutputExportService::GetStatus(RequestId requestId) const -> std::optional<RenderOutputExportStatus> {
  std::lock_guard lock(m_Mutex);
  const auto request =
      std::ranges::find_if(m_Requests, [requestId](const Request& item) { return item.Status.RequestId == requestId; });
  if (request == m_Requests.end()) {
    return std::nullopt;
  }
  return request->Status;
}

void RenderOutputExportService::Shutdown() {
  RenderSystem* renderer = nullptr;
  std::optional<RenderSystem::FinalRenderTicket> ticket;
  {
    std::lock_guard lock(m_Mutex);
    if (m_ShuttingDown) {
      return;
    }
    m_ShuttingDown = true;
    renderer = m_Renderer;
    if (m_ActiveReadback.has_value()) {
      const RequestId id = *m_ActiveReadback;
      const auto request =
          std::ranges::find_if(m_Requests, [id](const Request& item) { return item.Status.RequestId == id; });
      if (request != m_Requests.end()) {
        ticket = request->RenderTicket;
        request->Status.Phase = RenderOutputExportPhase::Failed;
        request->Status.Message = "Export cancelled during shutdown.";
      }
      m_ActiveReadback.reset();
    }
  }
  if (renderer != nullptr && ticket.has_value()) {
    const Engine::Status cancellationStatus = renderer->CancelFinalRender(*ticket);
    if (!cancellationStatus) {
      std::lock_guard lock(m_Mutex);
      const auto request =
          std::ranges::find_if(m_Requests, [ticket](const Request& item) { return item.RenderTicket == *ticket; });
      if (request != m_Requests.end()) {
        request->Status.Message += " Cancellation failed: " + cancellationStatus.error().Message;
      }
    }
  }
  m_WorkAvailable.notify_all();
  if (m_Worker.joinable()) {
    m_Worker.join();
  }
}

void RenderOutputExportService::WorkerMain() {
  for (;;) {
    WriteTask task;
    {
      std::unique_lock lock(m_Mutex);
      m_WorkAvailable.wait(lock, [this] { return m_ShuttingDown || !m_WriteQueue.empty(); });
      if (m_WriteQueue.empty()) {
        if (m_ShuttingDown) {
          return;
        }
        continue;
      }
      task = std::move(m_WriteQueue.front());
      m_WriteQueue.pop_front();
    }

    const Engine::Status writeStatus =
        task.IsExr ? RenderOutputExporter::WriteMultilayerExr(task.Output, task.SelectedAovs, task.Destination,
                                                              task.Background)
                   : RenderOutputExporter::WritePng(task.Output, task.Destination, task.Background);
    std::lock_guard lock(m_Mutex);
    const auto request =
        std::ranges::find_if(m_Requests, [&task](const Request& item) { return item.Status.RequestId == task.Id; });
    if (request == m_Requests.end()) {
      continue;
    }
    if (writeStatus) {
      request->Status.Phase = RenderOutputExportPhase::Completed;
      request->Status.Progress = 1.0F;
      request->Status.Message = "Saved " + request->Status.Destination.string();
    } else {
      request->Status.Phase = RenderOutputExportPhase::Failed;
      request->Status.Progress = 0.5F;
      request->Status.Message = "Image file write failed: " + writeStatus.error().Message;
    }
  }
}

} // namespace Engine

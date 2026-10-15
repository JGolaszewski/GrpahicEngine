#include "panels/include/StatisticsPanel.h"

#include "rendering/include/RenderBackend.h"
#include "runtime/include/Application.h"
#include "runtime/include/LogEngine.h"
#include "runtime/include/LogMacros.h"
#include <algorithm>
#include <array>
#include <imgui.h>
#include <string_view>
#include <vector>

namespace {
auto Percentile(const std::vector<float>& sortedValues, size_t valueCount, float percentile) -> float {
  if (valueCount == 0) {
    return 0.0F;
  }

  const size_t index = static_cast<size_t>(static_cast<float>(valueCount - 1) * percentile);
  return sortedValues.at(index);
}

void LogRecentDiagnosticRecords() {
  const std::vector<Engine::Log::LogRecord> records = Engine::Log::GetRecentRecords();
  const size_t firstRecord = records.size() > 3 ? records.size() - 3 : 0;
  for (size_t index = firstRecord; index < records.size(); ++index) {
    ENGINE_LOG_INFO_CODE("Performance", "performance.capture.recent_record",
                         "Recent diagnostic record | frame: {} | error_code: {} | text: {}",
                         records.at(index).event.frameIndex, Engine::Log::ErrorCodeView(records.at(index).event),
                         records.at(index).text);
  }
}

void LogBackendSnapshot(const Engine::Application* application) {
  const Engine::IRenderBackend* backend = application->GetRenderer();
  if (backend == nullptr) {
    return;
  }

  const Engine::BackendDiagnostics diagnostics = backend->GetDiagnostics();
  ENGINE_LOG_INFO_CODE("Performance", "performance.capture.backend",
                       "Backend snapshot | backend: {} | device_ready: {} | generation: {} | adapter_vendor: 0x{:X} | "
                       "adapter_device: 0x{:X} | dimensions: {}x{} | vsync: {}",
                       diagnostics.BackendName, diagnostics.DeviceReady, diagnostics.DeviceGeneration,
                       diagnostics.AdapterVendorId, diagnostics.AdapterDeviceId, diagnostics.Width, diagnostics.Height,
                       diagnostics.VSyncEnabled);
  ENGINE_LOG_INFO_CODE("Performance", "performance.capture.backend_state",
                       "Backend error state | present_result: 0x{:X} | device_removed_reason: 0x{:X} | "
                       "last_backend_error: 0x{:X} | validation_warnings: {} | validation_errors: {}",
                       diagnostics.LastPresentResult, diagnostics.DeviceRemovalReason, diagnostics.LastBackendError,
                       diagnostics.ValidationWarningCount, diagnostics.ValidationErrorCount);
}
} // namespace

// Rendering the diagnostics panel keeps related live counters and actions together.
// Dear ImGui's public text API is printf-style and cannot be wrapped without changing its contract.
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void StatWindow::Draw() {
  ImGui::Begin("Statistics", nullptr);
  if (m_Application == nullptr) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::TextDisabled("Application metrics are unavailable.");
    ImGui::End();
    return;
  }

  const auto& timing = m_Application->GetFrameTimingStats();
  const auto& history = m_Application->GetFrameMetricsHistory();
  const size_t applicationHistoryCount = m_Application->GetFrameMetricsCount();
  if (ImGui::Button(m_HistoryPaused ? "Resume history" : "Pause history")) {
    m_HistoryPaused = !m_HistoryPaused;
    if (m_HistoryPaused) {
      m_PausedHistoryCount = applicationHistoryCount;
      const size_t historyStart =
          (timing.FrameCount - applicationHistoryCount) % Engine::Application::FrameMetricsHistoryCapacity;
      for (size_t i = 0; i < applicationHistoryCount; ++i) {
        const auto& sample = history.at((historyStart + i) % Engine::Application::FrameMetricsHistoryCapacity);
        m_PausedHistory.at(i) = {
            .FrameIndex = sample.FrameIndex,
            .TimestampNs = sample.TimestampNs,
            .FrameTimeMs = sample.Timing.FrameTimeMs,
            .GpuTimeMs = sample.Timing.GpuFrameTimeMs,
            .GpuAvailable = sample.Timing.GpuTimingAvailable,
        };
      }
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset history")) {
    m_HistoryResetAfterFrame = timing.FrameCount;
    m_PausedHistoryCount = 0;
  }

  const std::array<const char*, 3> historyWindows = {"1 second", "5 seconds", "30 seconds"};
  constexpr std::array<int, 3> historyWindowValues = {1, 5, 30};
  const auto selectedHistoryWindow = std::ranges::find(historyWindowValues, m_HistoryWindowSeconds);
  int historyWindowIndex = static_cast<int>(std::distance(historyWindowValues.begin(), selectedHistoryWindow));
  if (selectedHistoryWindow == historyWindowValues.end()) {
    historyWindowIndex = 0;
  }
  if (ImGui::Combo("History window", &historyWindowIndex, historyWindows.data(),
                   static_cast<int>(historyWindows.size()))) {
    m_HistoryWindowSeconds = historyWindowValues.at(static_cast<size_t>(historyWindowIndex));
  }
  const size_t availableSamples = m_HistoryPaused ? m_PausedHistoryCount : applicationHistoryCount;
  const size_t firstIndex = (timing.FrameCount - availableSamples) % Engine::Application::FrameMetricsHistoryCapacity;
  std::vector<float> frameTimes(Engine::Application::FrameMetricsHistoryCapacity);
  std::vector<float> frameTimePlot(Engine::Application::FrameMetricsHistoryCapacity);
  std::vector<float> fpsPlot(Engine::Application::FrameMetricsHistoryCapacity);
  std::vector<float> gpuTimePlot(Engine::Application::FrameMetricsHistoryCapacity);
  uint32_t overBudgetCount = 0;
  size_t gpuSampleCount = 0;

  size_t sampleCount = 0;
  uint64_t latestTimestamp = 0;
  if (availableSamples > 0) {
    if (m_HistoryPaused) {
      latestTimestamp = m_PausedHistory.at(availableSamples - 1).TimestampNs;
    } else {
      latestTimestamp =
          history.at((timing.FrameCount - 1) % Engine::Application::FrameMetricsHistoryCapacity).TimestampNs;
    }
  }
  const uint64_t historyWindowNs = static_cast<uint64_t>(m_HistoryWindowSeconds) * 1000000000ULL;
  const uint64_t earliestTimestamp = latestTimestamp > historyWindowNs ? latestTimestamp - historyWindowNs : 0;
  for (size_t i = 0; i < availableSamples; ++i) {
    HistoryPoint point;
    if (m_HistoryPaused) {
      point = m_PausedHistory.at(i);
    } else {
      const auto& sample = history.at((firstIndex + i) % Engine::Application::FrameMetricsHistoryCapacity);
      point = {
          .FrameIndex = sample.FrameIndex,
          .TimestampNs = sample.TimestampNs,
          .FrameTimeMs = sample.Timing.FrameTimeMs,
          .GpuTimeMs = sample.Timing.GpuFrameTimeMs,
          .GpuAvailable = sample.Timing.GpuTimingAvailable,
      };
    }
    if (point.TimestampNs < earliestTimestamp || point.FrameIndex < m_HistoryResetAfterFrame) {
      continue;
    }
    const float frameTime = point.FrameTimeMs;
    frameTimes.at(sampleCount) = frameTime;
    frameTimePlot.at(sampleCount) = frameTime;
    fpsPlot.at(sampleCount) = frameTime > 0.0F ? 1000.0F / frameTime : 0.0F;
    if (point.GpuAvailable) {
      gpuTimePlot.at(sampleCount) = point.GpuTimeMs;
      ++gpuSampleCount;
    }
    overBudgetCount += static_cast<uint32_t>(frameTime > m_FrameBudgetMs);
    ++sampleCount;
  }

  const uint64_t frameIndex = timing.FrameCount == 0 ? 0 : timing.FrameCount - 1;
  std::sort(frameTimes.begin(), frameTimes.begin() + static_cast<std::ptrdiff_t>(sampleCount));
  float latestGpuTimeMs = 0.0F;
  bool hasGpuTime = false;
  for (size_t i = 0; i < sampleCount; ++i) {
    if (gpuTimePlot.at(i) > 0.0F) {
      latestGpuTimeMs = gpuTimePlot.at(i);
      hasGpuTime = true;
    } else if (hasGpuTime) {
      gpuTimePlot.at(i) = latestGpuTimeMs;
    }
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::Text("Completed frames: %llu | current frame index: %llu", static_cast<unsigned long long>(timing.FrameCount),
              static_cast<unsigned long long>(frameIndex));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::Text("Frame time: %.2f ms (%.1f FPS)", timing.FrameTimeMs,
              timing.FrameTimeMs > 0.0F ? 1000.0F / timing.FrameTimeMs : 0.0F);
  const float imguiFps = ImGui::GetIO().Framerate;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::Text("ImGui smoothed FPS: %.1f (%.2f ms)", imguiFps, imguiFps > 0.0F ? 1000.0F / imguiFps : 0.0F);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::Text("CPU frame total (incl. Present): %.2f ms | limiter wait: %.2f ms", timing.CpuFrameTimeMs,
              timing.LimiterWaitMs);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::Text("Rolling frame time: median %.2f ms | p95 %.2f ms | p99 %.2f ms",
              Percentile(frameTimes, sampleCount, 0.50F), Percentile(frameTimes, sampleCount, 0.95F),
              Percentile(frameTimes, sampleCount, 0.99F));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::Text("Rolling min/max: %.2f / %.2f ms | clamped delta frames: %llu",
              sampleCount == 0 ? 0.0F : frameTimes.front(), sampleCount == 0 ? 0.0F : frameTimes.at(sampleCount - 1),
              static_cast<unsigned long long>(timing.ClampedFrameCount));

  ImGui::SeparatorText("Frame phases");
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::Text("Update and simulation: %.2f ms", timing.UpdateTimeMs);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::Text("View setup: %.2f ms", timing.ViewSetupTimeMs);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::Text("Render submission and graph: %.2f ms", timing.RenderTimeMs);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::Text("UI: %.2f ms | Present: %.2f ms", timing.UiTimeMs, timing.PresentTimeMs);
  if (timing.GpuTimingAvailable) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::Text("GPU render work: %.2f ms (query for frame %llu)", timing.GpuFrameTimeMs,
                static_cast<unsigned long long>(timing.GpuTimingFrameIndex));
  } else {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::TextDisabled("GPU render work: %s", timing.GpuTimingPending ? "pending" : "unavailable");
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::TextDisabled("Per-pass GPU timing: unavailable");

  ImGui::SeparatorText("Workload and budget");
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::Text("Views: %u | objects: %u submitted, %u culled | packets: %u", timing.ViewsSubmitted,
              timing.ObjectsSubmitted, timing.ObjectsCulled, timing.PacketsBuilt);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::Text("Packet queues: %u opaque, %u masked, %u transparent", timing.OpaquePackets, timing.MaskedPackets,
              timing.TransparentPackets);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::Text("Draw calls: %u | uploads: %llu bytes | buffer allocations: %u", timing.DrawCalls,
              static_cast<unsigned long long>(timing.UploadBytes), timing.BufferAllocations);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::Text("Culling: %.2f ms | packet build: %.2f ms", timing.CullingTimeMs, timing.PacketBuildTimeMs);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::Text("Pipeline cache: %u hits, %u misses, %u created", timing.PipelineCacheHits, timing.PipelineCacheMisses,
              timing.PipelineCreations);
  ImGui::SetNextItemWidth(160.0F);
  ImGui::DragFloat("Frame budget (ms)", &m_FrameBudgetMs, 0.1F, 1.0F, 100.0F, "%.2f");
  ImGui::SameLine();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::Text("Over budget: %u / %llu recent frames", overBudgetCount, static_cast<unsigned long long>(sampleCount));

  if (sampleCount > 0 && timing.FrameTimeMs > m_FrameBudgetMs) {
    ++m_OverBudgetStreak;
  } else {
    m_OverBudgetStreak = 0;
  }

  if (m_CaptureRequested && sampleCount > 0) {
    ENGINE_LOG_INFO_CODE("Performance", "performance.manual_capture",
                         "Performance snapshot | frame: {} | frame_ms: {:.3f} | gpu_available: {} | gpu_ms: {:.3f} | "
                         "gpu_frame: {} | update_ms: {:.3f} | view_setup_ms: {:.3f} | render_ms: {:.3f}",
                         frameIndex, timing.FrameTimeMs, timing.GpuTimingAvailable, timing.GpuFrameTimeMs,
                         timing.GpuTimingFrameIndex, timing.UpdateTimeMs, timing.ViewSetupTimeMs, timing.RenderTimeMs);
    ENGINE_LOG_INFO_CODE(
        "Performance", "performance.manual_capture.workload",
        "Performance workload | ui_ms: {:.3f} | present_ms: {:.3f} | limiter_wait_ms: {:.3f} | objects_submitted: {} | "
        "objects_culled: {} | packets_built: {} | draw_calls: {} | upload_bytes: {}",
        timing.UiTimeMs, timing.PresentTimeMs, timing.LimiterWaitMs, timing.ObjectsSubmitted, timing.ObjectsCulled,
        timing.PacketsBuilt, timing.DrawCalls, timing.UploadBytes);
    ENGINE_LOG_INFO_CODE("Performance", "performance.manual_capture.pipeline",
                         "Pipeline cache activity | hits: {} | misses: {} | created: {}", timing.PipelineCacheHits,
                         timing.PipelineCacheMisses, timing.PipelineCreations);
    LogRecentDiagnosticRecords();
    LogBackendSnapshot(m_Application);
    m_CaptureRequested = false;
  }

  if (m_OverBudgetStreak >= 30 &&
      (m_LastRegressionLogFrame == 0 || timing.FrameCount - m_LastRegressionLogFrame >= 300)) {
    ENGINE_LOG_WARN_CODE(
        "Performance", "performance.frame_budget.regression",
        "Frame budget exceeded for consecutive frames | frame: {} | frame_ms: {:.3f} | budget_ms: {:.3f} | "
        "gpu_available: {} | gpu_ms: {:.3f} | update_ms: {:.3f} | view_setup_ms: {:.3f} | render_ms: {:.3f}",
        frameIndex, timing.FrameTimeMs, m_FrameBudgetMs, timing.GpuTimingAvailable, timing.GpuFrameTimeMs,
        timing.UpdateTimeMs, timing.ViewSetupTimeMs, timing.RenderTimeMs);
    ENGINE_LOG_WARN_CODE(
        "Performance", "performance.frame_budget.workload",
        "Over-budget frame workload | ui_ms: {:.3f} | present_ms: {:.3f} | limiter_wait_ms: {:.3f} | "
        "objects_submitted: {} | objects_culled: {} | packets_built: {} | draw_calls: {} | upload_bytes: {}",
        timing.UiTimeMs, timing.PresentTimeMs, timing.LimiterWaitMs, timing.ObjectsSubmitted, timing.ObjectsCulled,
        timing.PacketsBuilt, timing.DrawCalls, timing.UploadBytes);
    LogRecentDiagnosticRecords();
    LogBackendSnapshot(m_Application);
    m_LastRegressionLogFrame = timing.FrameCount;
  }

  if (ImGui::Button("Capture current frame to log")) {
    m_CaptureRequested = true;
  }
  if (sampleCount > 0) {
    ImGui::PlotLines("Frame time (ms)", frameTimePlot.data(), static_cast<int>(sampleCount), 0, nullptr, 0.0F,
                     (std::max)(m_FrameBudgetMs * 2.0F, 1.0F), ImVec2(0, 65));
    ImGui::PlotLines("FPS", fpsPlot.data(), static_cast<int>(sampleCount), 0, nullptr, 0.0F, 240.0F, ImVec2(0, 55));
    if (gpuSampleCount > 0) {
      ImGui::PlotLines("GPU render query (last completed, held between queries)", gpuTimePlot.data(),
                       static_cast<int>(sampleCount), 0, nullptr, 0.0F, (std::max)(m_FrameBudgetMs * 2.0F, 1.0F),
                       ImVec2(0, 55));
    }
  }

  if (const Engine::IRenderBackend* backend = m_Application->GetRenderer()) {
    const Engine::BackendDiagnostics diagnostics = backend->GetDiagnostics();
    ImGui::SeparatorText("Backend");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::Text("%s | device %s | %u x %u | generation %llu", diagnostics.BackendName,
                diagnostics.DeviceReady ? "ready" : "unavailable", diagnostics.Width, diagnostics.Height,
                static_cast<unsigned long long>(diagnostics.DeviceGeneration));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::Text("Adapter vendor/device: 0x%04X / 0x%04X | last present: %.2f ms", diagnostics.AdapterVendorId,
                diagnostics.AdapterDeviceId, diagnostics.LastPresentTimeMs);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::Text("Validation warnings: %llu | errors: %llu",
                static_cast<unsigned long long>(diagnostics.ValidationWarningCount),
                static_cast<unsigned long long>(diagnostics.ValidationErrorCount));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::Text("Last backend error: 0x%08X", static_cast<uint32_t>(diagnostics.LastBackendError));
    if (diagnostics.HasVSync) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic
      // API.
      ImGui::Text("VSync: %s | Present result: 0x%08X | device removal: 0x%08X",
                  diagnostics.VSyncEnabled ? "on" : "off", static_cast<uint32_t>(diagnostics.LastPresentResult),
                  static_cast<uint32_t>(diagnostics.DeviceRemovalReason));
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::Text("Simulation: %s | frame limiter: %u Hz", m_Application->IsSimulationPaused() ? "paused" : "running",
                m_Application->GetFrameLimiterHz());
  }

  if (ImGui::CollapsingHeader("Recent diagnostic markers")) {
    const std::vector<Engine::Log::LogRecord> records = Engine::Log::GetRecentRecords();
    size_t shown = 0;
    for (auto record = records.rbegin(); record != records.rend() && shown < 8; ++record) {
      const bool marker = record->event.level >= Engine::Log::Level::Warning ||
                          record->event.category == Engine::Log::Category::Backend ||
                          record->event.category == Engine::Log::Category::Asset ||
                          Engine::Log::ErrorCodeView(record->event).contains("performance.");
      if (!marker) {
        continue;
      }
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic
      // API.
      const std::string errorCode(Engine::Log::ErrorCodeView(record->event));
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui's documented text API uses printf-style
      // formatting.
      ImGui::TextWrapped("Frame %llu | %s | %s | %s", static_cast<unsigned long long>(record->event.frameIndex),
                         Engine::Log::ToString(record->event.category), errorCode.c_str(), record->text.c_str());
      ++shown;
    }
    if (shown == 0) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic
      // API.
      ImGui::TextDisabled("No recent error, backend, asset, or performance events.");
    }
  }

  ImGui::End();
}
// NOLINTEND(cppcoreguidelines-pro-type-vararg)

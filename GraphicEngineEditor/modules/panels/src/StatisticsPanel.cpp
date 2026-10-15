#include "panels/include/StatisticsPanel.h"

#include "runtime/include/Application.h"
#include "runtime/include/LogEngine.h"
#include "runtime/include/LogMacros.h"
#include "rendering/include/RenderBackend.h"
#include <algorithm>
#include <array>
#include <imgui.h>
#include <iterator>
#include <string_view>
#include <vector>

namespace
{
float Percentile(const std::vector<float> &sortedValues, size_t valueCount, float percentile)
{
    if (valueCount == 0)
        return 0.0f;

    const double boundedPercentile = (std::clamp)(static_cast<double>(percentile), 0.0, 1.0);
    const size_t index = static_cast<size_t>(static_cast<double>(valueCount - 1) * boundedPercentile);
    return sortedValues[index];
}

void LogRecentDiagnosticRecords()
{
    const std::vector<Engine::Log::LogRecord> records = Engine::Log::GetRecentRecords();
    const size_t firstRecord = records.size() > 3 ? records.size() - 3 : 0;
    for (size_t index = firstRecord; index < records.size(); ++index)
    {
        ENGINE_LOG_INFO_CODE("Performance", "performance.capture.recent_record",
                             "Recent diagnostic record | frame: {} | error_code: {} | text: {}",
                             records[index].event.frameIndex, records[index].event.errorCode,
                             records[index].text);
    }
}

void LogBackendSnapshot(const Engine::Application *application)
{
    const Engine::IRenderBackend *backend = application->GetRenderer();
    if (!backend)
        return;

    const Engine::BackendDiagnostics diagnostics = backend->GetDiagnostics();
    ENGINE_LOG_INFO_CODE("Performance", "performance.capture.backend",
                         "Backend snapshot | backend: {} | device_ready: {} | generation: {} | "
                         "adapter_vendor: 0x{:X} | adapter_device: 0x{:X} | dimensions: {}x{} | vsync: {}",
                         diagnostics.BackendName, diagnostics.DeviceReady, diagnostics.DeviceGeneration,
                         diagnostics.AdapterVendorId, diagnostics.AdapterDeviceId, diagnostics.Width,
                         diagnostics.Height, diagnostics.VSyncEnabled);
    ENGINE_LOG_INFO_CODE("Performance", "performance.capture.backend_state",
                         "Backend error state | present_result: 0x{:X} | device_removed_reason: 0x{:X} | "
                         "last_backend_error: 0x{:X} | validation_warnings: {} | validation_errors: {}",
                         diagnostics.LastPresentResult, diagnostics.DeviceRemovalReason,
                         diagnostics.LastBackendError, diagnostics.ValidationWarningCount,
                         diagnostics.ValidationErrorCount);
}
} // namespace

void StatWindow::Draw()
{
    ImGui::Begin("Statistics", nullptr);
    if (!m_Application)
    {
        ImGui::TextDisabled("Application metrics are unavailable.");
        ImGui::End();
        return;
    }

    const auto &timing = m_Application->GetFrameTimingStats();
    const auto &history = m_Application->GetFrameMetricsHistory();
    const size_t applicationHistoryCount = m_Application->GetFrameMetricsCount();
    if (ImGui::Button(m_HistoryPaused ? "Resume history" : "Pause history"))
    {
        m_HistoryPaused = !m_HistoryPaused;
        if (m_HistoryPaused)
        {
            m_PausedHistoryCount = applicationHistoryCount;
            const size_t historyStart = (timing.FrameCount - applicationHistoryCount) %
                                        Engine::Application::FrameMetricsHistoryCapacity;
            for (size_t i = 0; i < applicationHistoryCount; ++i)
            {
                const auto &sample =
                    history[(historyStart + i) % Engine::Application::FrameMetricsHistoryCapacity];
                m_PausedHistory[i] = {sample.FrameIndex, sample.TimestampNs, sample.Timing.FrameTimeMs,
                                      sample.Timing.GpuFrameTimeMs, sample.Timing.GpuTimingAvailable};
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset history"))
    {
        m_HistoryResetAfterFrame = timing.FrameCount;
        m_PausedHistoryCount = 0;
    }

    const char *historyWindows[] = {"1 second", "5 seconds", "30 seconds"};
    int historyWindowIndex = m_HistoryWindowSeconds == 1 ? 0 : (m_HistoryWindowSeconds == 5 ? 1 : 2);
    if (ImGui::Combo("History window", &historyWindowIndex, historyWindows, 3))
        m_HistoryWindowSeconds = historyWindowIndex == 0 ? 1 : (historyWindowIndex == 1 ? 5 : 30);
    const size_t availableSamples = m_HistoryPaused ? m_PausedHistoryCount : applicationHistoryCount;
    const size_t firstIndex =
        (timing.FrameCount - availableSamples) % Engine::Application::FrameMetricsHistoryCapacity;
    std::vector<float> frameTimes(Engine::Application::FrameMetricsHistoryCapacity);
    std::vector<float> frameTimePlot(Engine::Application::FrameMetricsHistoryCapacity);
    std::vector<float> fpsPlot(Engine::Application::FrameMetricsHistoryCapacity);
    std::vector<float> gpuTimePlot(Engine::Application::FrameMetricsHistoryCapacity);
    uint32_t overBudgetCount = 0;
    size_t gpuSampleCount = 0;

    size_t sampleCount = 0;
    const uint64_t latestTimestamp =
        availableSamples == 0
            ? 0
            : (m_HistoryPaused
                   ? m_PausedHistory[availableSamples - 1].TimestampNs
                   : history[(timing.FrameCount - 1) % Engine::Application::FrameMetricsHistoryCapacity]
                         .TimestampNs);
    const uint64_t historyWindowNs = static_cast<uint64_t>(m_HistoryWindowSeconds) * 1000000000ull;
    const uint64_t earliestTimestamp =
        latestTimestamp > historyWindowNs ? latestTimestamp - historyWindowNs : 0;
    for (size_t i = 0; i < availableSamples; ++i)
    {
        HistoryPoint point;
        if (m_HistoryPaused)
        {
            point = m_PausedHistory[i];
        }
        else
        {
            const auto &sample = history[(firstIndex + i) % Engine::Application::FrameMetricsHistoryCapacity];
            point = {sample.FrameIndex, sample.TimestampNs, sample.Timing.FrameTimeMs,
                     sample.Timing.GpuFrameTimeMs, sample.Timing.GpuTimingAvailable};
        }
        if (point.TimestampNs < earliestTimestamp || point.FrameIndex < m_HistoryResetAfterFrame)
            continue;
        const float frameTime = point.FrameTimeMs;
        frameTimes[sampleCount] = frameTime;
        frameTimePlot[sampleCount] = frameTime;
        fpsPlot[sampleCount] = frameTime > 0.0f ? 1000.0f / frameTime : 0.0f;
        if (point.GpuAvailable)
        {
            gpuTimePlot[sampleCount] = point.GpuTimeMs;
            ++gpuSampleCount;
        }
        overBudgetCount += frameTime > m_FrameBudgetMs;
        ++sampleCount;
    }

    const uint64_t frameIndex = timing.FrameCount == 0 ? 0 : timing.FrameCount - 1;
    const auto sampleEnd = std::next(frameTimes.begin(), static_cast<std::ptrdiff_t>(sampleCount));
    std::sort(frameTimes.begin(), sampleEnd);
    float latestGpuTimeMs = 0.0f;
    bool hasGpuTime = false;
    for (size_t i = 0; i < sampleCount; ++i)
    {
        if (gpuTimePlot[i] > 0.0f)
        {
            latestGpuTimeMs = gpuTimePlot[i];
            hasGpuTime = true;
        }
        else if (hasGpuTime)
        {
            gpuTimePlot[i] = latestGpuTimeMs;
        }
    }
    ImGui::Text("Completed frames: %llu | current frame index: %llu",
                static_cast<unsigned long long>(timing.FrameCount),
                static_cast<unsigned long long>(frameIndex));
    ImGui::Text("Frame time: %.2f ms (%.1f FPS)", timing.FrameTimeMs,
                timing.FrameTimeMs > 0.0f ? 1000.0f / timing.FrameTimeMs : 0.0f);
    const float imguiFps = ImGui::GetIO().Framerate;
    ImGui::Text("ImGui smoothed FPS: %.1f (%.2f ms)", imguiFps, imguiFps > 0.0f ? 1000.0f / imguiFps : 0.0f);
    ImGui::Text("CPU frame total (incl. Present): %.2f ms | limiter wait: %.2f ms", timing.CpuFrameTimeMs,
                timing.LimiterWaitMs);
    ImGui::Text("Rolling frame time: median %.2f ms | p95 %.2f ms | p99 %.2f ms",
                Percentile(frameTimes, sampleCount, 0.50f), Percentile(frameTimes, sampleCount, 0.95f),
                Percentile(frameTimes, sampleCount, 0.99f));
    ImGui::Text("Rolling min/max: %.2f / %.2f ms | clamped delta frames: %llu",
                sampleCount == 0 ? 0.0f : frameTimes.front(),
                sampleCount == 0 ? 0.0f : frameTimes[sampleCount - 1],
                static_cast<unsigned long long>(timing.ClampedFrameCount));

    ImGui::SeparatorText("Frame phases");
    ImGui::Text("Update and simulation: %.2f ms", timing.UpdateTimeMs);
    ImGui::Text("View setup: %.2f ms", timing.ViewSetupTimeMs);
    ImGui::Text("Render submission and graph: %.2f ms", timing.RenderTimeMs);
    ImGui::Text("UI: %.2f ms | Present: %.2f ms", timing.UiTimeMs, timing.PresentTimeMs);
    if (timing.GpuTimingAvailable)
        ImGui::Text("GPU render work: %.2f ms (query for frame %llu)", timing.GpuFrameTimeMs,
                    static_cast<unsigned long long>(timing.GpuTimingFrameIndex));
    else
        ImGui::TextDisabled("GPU render work: %s", timing.GpuTimingPending ? "pending" : "unavailable");
    ImGui::TextDisabled("Per-pass GPU timing: unavailable");

    ImGui::SeparatorText("Workload and budget");
    ImGui::Text("Views: %u | objects: %u submitted, %u culled | packets: %u", timing.ViewsSubmitted,
                timing.ObjectsSubmitted, timing.ObjectsCulled, timing.PacketsBuilt);
    ImGui::Text("Packet queues: %u opaque, %u masked, %u transparent", timing.OpaquePackets,
                timing.MaskedPackets, timing.TransparentPackets);
    ImGui::Text("Draw calls: %u | uploads: %llu bytes | buffer allocations: %u", timing.DrawCalls,
                static_cast<unsigned long long>(timing.UploadBytes), timing.BufferAllocations);
    ImGui::Text("Culling: %.2f ms | packet build: %.2f ms", timing.CullingTimeMs, timing.PacketBuildTimeMs);
    ImGui::Text("Pipeline cache: %u hits, %u misses, %u created", timing.PipelineCacheHits,
                timing.PipelineCacheMisses, timing.PipelineCreations);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat("Frame budget (ms)", &m_FrameBudgetMs, 0.1f, 1.0f, 100.0f, "%.2f");
    ImGui::SameLine();
    ImGui::Text("Over budget: %u / %llu recent frames", overBudgetCount,
                static_cast<unsigned long long>(sampleCount));

    if (sampleCount > 0 && timing.FrameTimeMs > m_FrameBudgetMs)
        ++m_OverBudgetStreak;
    else
        m_OverBudgetStreak = 0;

    if (m_CaptureRequested && sampleCount > 0)
    {
        ENGINE_LOG_INFO_CODE(
            "Performance", "performance.manual_capture",
            "Performance snapshot | frame: {} | frame_ms: {:.3f} | gpu_available: {} | gpu_ms: {:.3f} | "
            "gpu_frame: {} | update_ms: {:.3f} | view_setup_ms: {:.3f} | render_ms: {:.3f}",
            frameIndex, timing.FrameTimeMs, timing.GpuTimingAvailable, timing.GpuFrameTimeMs,
            timing.GpuTimingFrameIndex, timing.UpdateTimeMs, timing.ViewSetupTimeMs, timing.RenderTimeMs);
        ENGINE_LOG_INFO_CODE("Performance", "performance.manual_capture.workload",
                             "Performance workload | ui_ms: {:.3f} | present_ms: {:.3f} | limiter_wait_ms: "
                             "{:.3f} | objects_submitted: {} | objects_culled: {} | packets_built: {} | "
                             "draw_calls: {} | upload_bytes: {}",
                             timing.UiTimeMs, timing.PresentTimeMs, timing.LimiterWaitMs,
                             timing.ObjectsSubmitted, timing.ObjectsCulled, timing.PacketsBuilt,
                             timing.DrawCalls, timing.UploadBytes);
        ENGINE_LOG_INFO_CODE("Performance", "performance.manual_capture.pipeline",
                             "Pipeline cache activity | hits: {} | misses: {} | created: {}",
                             timing.PipelineCacheHits, timing.PipelineCacheMisses, timing.PipelineCreations);
        LogRecentDiagnosticRecords();
        LogBackendSnapshot(m_Application);
        m_CaptureRequested = false;
    }

    if (m_OverBudgetStreak >= 30 &&
        (m_LastRegressionLogFrame == 0 || timing.FrameCount - m_LastRegressionLogFrame >= 300))
    {
        ENGINE_LOG_WARN_CODE("Performance", "performance.frame_budget.regression",
                             "Frame budget exceeded for consecutive frames | frame: {} | frame_ms: {:.3f} | "
                             "budget_ms: {:.3f} | gpu_available: {} | gpu_ms: {:.3f} | update_ms: {:.3f} | "
                             "view_setup_ms: {:.3f} | render_ms: {:.3f}",
                             frameIndex, timing.FrameTimeMs, m_FrameBudgetMs, timing.GpuTimingAvailable,
                             timing.GpuFrameTimeMs, timing.UpdateTimeMs, timing.ViewSetupTimeMs,
                             timing.RenderTimeMs);
        ENGINE_LOG_WARN_CODE("Performance", "performance.frame_budget.workload",
                             "Over-budget frame workload | ui_ms: {:.3f} | present_ms: {:.3f} | "
                             "limiter_wait_ms: {:.3f} | objects_submitted: {} | objects_culled: {} | "
                             "packets_built: {} | draw_calls: {} | upload_bytes: {}",
                             timing.UiTimeMs, timing.PresentTimeMs, timing.LimiterWaitMs,
                             timing.ObjectsSubmitted, timing.ObjectsCulled, timing.PacketsBuilt,
                             timing.DrawCalls, timing.UploadBytes);
        LogRecentDiagnosticRecords();
        LogBackendSnapshot(m_Application);
        m_LastRegressionLogFrame = timing.FrameCount;
    }

    if (ImGui::Button("Capture current frame to log"))
        m_CaptureRequested = true;
    if (sampleCount > 0)
    {
        ImGui::PlotLines("Frame time (ms)", frameTimePlot.data(), static_cast<int>(sampleCount), 0, nullptr,
                         0.0f, (std::max)(m_FrameBudgetMs * 2.0f, 1.0f), ImVec2(0, 65));
        ImGui::PlotLines("FPS", fpsPlot.data(), static_cast<int>(sampleCount), 0, nullptr, 0.0f, 240.0f,
                         ImVec2(0, 55));
        if (gpuSampleCount > 0)
        {
            ImGui::PlotLines("GPU render query (last completed, held between queries)", gpuTimePlot.data(),
                             static_cast<int>(sampleCount), 0, nullptr, 0.0f,
                             (std::max)(m_FrameBudgetMs * 2.0f, 1.0f), ImVec2(0, 55));
        }
    }

    if (const Engine::IRenderBackend *backend = m_Application->GetRenderer())
    {
        const Engine::BackendDiagnostics diagnostics = backend->GetDiagnostics();
        ImGui::SeparatorText("Backend");
        ImGui::Text("%s | device %s | %u x %u | generation %llu", diagnostics.BackendName,
                    diagnostics.DeviceReady ? "ready" : "unavailable", diagnostics.Width, diagnostics.Height,
                    static_cast<unsigned long long>(diagnostics.DeviceGeneration));
        ImGui::Text("Adapter vendor/device: 0x%04X / 0x%04X | last present: %.2f ms",
                    diagnostics.AdapterVendorId, diagnostics.AdapterDeviceId, diagnostics.LastPresentTimeMs);
        ImGui::Text("Validation warnings: %llu | errors: %llu",
                    static_cast<unsigned long long>(diagnostics.ValidationWarningCount),
                    static_cast<unsigned long long>(diagnostics.ValidationErrorCount));
        ImGui::Text("Last backend error: 0x%08X", static_cast<uint32_t>(diagnostics.LastBackendError));
        if (diagnostics.HasVSync)
            ImGui::Text("VSync: %s | Present result: 0x%08X | device removal: 0x%08X",
                        diagnostics.VSyncEnabled ? "on" : "off",
                        static_cast<uint32_t>(diagnostics.LastPresentResult),
                        static_cast<uint32_t>(diagnostics.DeviceRemovalReason));
        ImGui::Text("Simulation: %s | frame limiter: %u Hz",
                    m_Application->IsSimulationPaused() ? "paused" : "running",
                    m_Application->GetFrameLimiterHz());
    }

    if (ImGui::CollapsingHeader("Recent diagnostic markers"))
    {
        const std::vector<Engine::Log::LogRecord> records = Engine::Log::GetRecentRecords();
        size_t shown = 0;
        for (auto record = records.rbegin(); record != records.rend() && shown < 8; ++record)
        {
            const bool marker =
                record->event.level >= Engine::Log::Level::Warning ||
                record->event.category == Engine::Log::Category::Backend ||
                record->event.category == Engine::Log::Category::Asset ||
                std::string_view(record->event.errorCode).find("performance.") != std::string_view::npos;
            if (!marker)
                continue;
            ImGui::TextWrapped(
                "Frame %llu | %s | %s | %s", static_cast<unsigned long long>(record->event.frameIndex),
                Engine::Log::ToString(record->event.category), record->event.errorCode, record->text.c_str());
            ++shown;
        }
        if (shown == 0)
            ImGui::TextDisabled("No recent error, backend, asset, or performance events.");
    }

    ImGui::End();
}

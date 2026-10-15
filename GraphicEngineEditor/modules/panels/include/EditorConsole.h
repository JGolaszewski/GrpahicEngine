#pragma once

#include "runtime/include/LogEngine.h"

#include <imgui.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct EditorConsoleEntry
{
    Engine::Log::LogEvent event{};
    std::string timestamp;
    std::string source;
    std::string message;
    std::string filterText;
};

class EditorConsoleSink
{
  public:
    static constexpr size_t Capacity = 10'000;

    void Push(const Engine::Log::LogRecord &record);
    void CopySnapshot(std::vector<std::shared_ptr<const EditorConsoleEntry>> &snapshot) const;
    uint64_t Generation() const noexcept;
    void Clear();
    void SetPauseOnError(bool enabled) noexcept;
    bool ConsumePauseRequest() noexcept;
    void SetCapturePaused(bool paused) noexcept;
    bool IsCapturePaused() const noexcept;
    uint64_t DroppedWhilePaused() const noexcept;
    uint64_t OverwrittenEntries() const noexcept;

  private:
    mutable std::mutex m_Mutex;
    std::array<std::shared_ptr<const EditorConsoleEntry>, Capacity> m_Entries{};
    size_t m_WriteIndex = 0;
    size_t m_Size = 0;
    std::atomic<uint64_t> m_Generation{0};
    std::atomic<bool> m_PauseOnError{false};
    std::atomic<bool> m_PauseRequested{false};
    std::atomic<bool> m_CapturePaused{false};
    std::atomic<uint64_t> m_DroppedWhilePaused{0};
    std::atomic<uint64_t> m_OverwrittenEntries{0};
};

class EditorConsole
{
  public:
    EditorConsole();
    ~EditorConsole();

    EditorConsole(const EditorConsole &) = delete;
    EditorConsole &operator=(const EditorConsole &) = delete;

    void AttachToLogger();
    void DetachFromLogger();
    bool ConsumePauseRequest() noexcept;
    void SetSimulationStateCallbacks(std::function<bool()> isPaused, std::function<void(bool)> setPaused);
    void SetDiagnosticContextProvider(std::function<std::string()> provider,
                                      std::function<uint64_t()> currentFrameProvider);
    void UpdateAndRender(bool *isOpen);

  private:
    std::shared_ptr<EditorConsoleSink> m_Sink;
    Engine::Log::CallbackToken m_LoggerSubscription = 0;
    std::vector<std::shared_ptr<const EditorConsoleEntry>> m_Snapshot;
    std::vector<std::shared_ptr<const EditorConsoleEntry>> m_VisibleEntries;
    uint64_t m_SnapshotGeneration = 0;
    ImGuiTextFilter m_Filter;
    std::array<bool, 6> m_VisibleLevels{true, true, true, true, true, true};
    std::array<bool, Engine::Log::CategoryCount> m_VisibleCategories{};
    std::function<std::string()> m_DiagnosticContextProvider;
    std::function<uint64_t()> m_CurrentFrameProvider;
    std::shared_ptr<const EditorConsoleEntry> m_SelectedEntry;
    std::function<bool()> m_IsSimulationPaused;
    std::function<void(bool)> m_SetSimulationPaused;
    bool m_AutoScroll = true;
    bool m_CapturePaused = false;
    bool m_RedactPaths = true;
    bool m_FrameRangeEnabled = false;
    bool m_TimeRangeEnabled = false;
    uint64_t m_FrameRangeMinimum = 0;
    uint64_t m_FrameRangeMaximum = UINT64_MAX;
    int m_TimeRangeSeconds = 30;
    int m_QuickFilter = 0;
    char m_ErrorCodeFilter[128]{};
    char m_SourceFilter[128]{};
    char m_ThreadFilter[32]{};
    std::string m_ExportError;
    bool m_PauseOnError = false;
    Engine::Log::Level m_RuntimeMinimumLevel = Engine::Log::Level::Trace;
    Engine::Log::Level m_RuntimeCategoryMinimumLevel = Engine::Log::Level::Trace;
    int m_RuntimeCategoryIndex = 0;
};

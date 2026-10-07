#pragma once

#include "LogEngine.h"

#include <imgui.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
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

  private:
    mutable std::mutex m_Mutex;
    std::array<std::shared_ptr<const EditorConsoleEntry>, Capacity> m_Entries{};
    size_t m_WriteIndex = 0;
    size_t m_Size = 0;
    std::atomic<uint64_t> m_Generation{0};
    std::atomic<bool> m_PauseOnError{false};
    std::atomic<bool> m_PauseRequested{false};
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
    void SetSimulationStateCallbacks(std::function<bool()> isPaused,
                                    std::function<void(bool)> setPaused);
    void UpdateAndRender(bool *isOpen);

  private:
    std::shared_ptr<EditorConsoleSink> m_Sink;
    std::vector<std::shared_ptr<const EditorConsoleEntry>> m_Snapshot;
    std::vector<std::shared_ptr<const EditorConsoleEntry>> m_VisibleEntries;
    uint64_t m_SnapshotGeneration = 0;
    ImGuiTextFilter m_Filter;
    std::array<bool, 6> m_VisibleLevels{true, true, true, true, true, true};
    std::function<bool()> m_IsSimulationPaused;
    std::function<void(bool)> m_SetSimulationPaused;
    bool m_AutoScroll = true;
    bool m_PauseOnError = false;
};

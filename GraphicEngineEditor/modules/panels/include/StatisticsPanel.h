#pragma once

#include "panels/include/EditorPanel.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Engine
{
class Application;
}

class StatWindow : public EditorPanel
{
  public:
    static constexpr size_t HistoryCapacity = 8192;

    explicit StatWindow(const Engine::Application *application) : m_Application(application)
    {
    }

    void Draw() override;

  private:
    struct HistoryPoint
    {
        uint64_t FrameIndex = 0;
        uint64_t TimestampNs = 0;
        float FrameTimeMs = 0.0f;
        float GpuTimeMs = 0.0f;
        bool GpuAvailable = false;
    };

    const Engine::Application *m_Application = nullptr;
    std::array<HistoryPoint, HistoryCapacity> m_PausedHistory{};
    size_t m_PausedHistoryCount = 0;
    float m_FrameBudgetMs = 16.67f;
    int m_HistoryWindowSeconds = 5;
    uint32_t m_OverBudgetStreak = 0;
    uint64_t m_LastRegressionLogFrame = 0;
    uint64_t m_HistoryResetAfterFrame = 0;
    bool m_CaptureRequested = false;
    bool m_HistoryPaused = false;
};

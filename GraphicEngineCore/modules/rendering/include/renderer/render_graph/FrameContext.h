#pragma once

#include "runtime/include/CoreExport.h"
#include "rendering/include/renderer/render_graph/FrameBlackboard.h"
#include "rendering/include/renderer/render_graph/RGResourcePool.h"
#include "rendering/include/renderer/render_graph/RenderView.h"

#include <array>
#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine::Renderer
{
struct FrameDiagnostics
{
    uint64_t UploadBytes = 0;
    uint32_t BufferAllocations = 0;
    uint32_t BindingSetAllocations = 0;
    uint32_t DrawCalls = 0;
};

struct GpuFrameTiming
{
    bool Available = false;
    bool Pending = false;
    uint64_t FrameIndex = 0;
    float TimeMs = 0.0f;
};

class ENGINE_API FrameContext
{
  public:
    static constexpr uint64_t FramesInFlight = 2;

    explicit FrameContext(nvrhi::IDevice *device);
    FrameContext(const FrameContext &) = delete;
    FrameContext &operator=(const FrameContext &) = delete;

    void BeginFrame();
    void MarkSubmitted();

    uint64_t GetFrameIndex() const noexcept
    {
        return m_FrameIndex;
    }
    uint64_t GetSubmittedFrames() const noexcept
    {
        return m_SubmittedFrames;
    }
    uint64_t GetCompletedFrames() const noexcept
    {
        return m_CompletedFrames;
    }
    FrameBlackboard &GetBlackboard() noexcept
    {
        return m_Blackboard;
    }
    const FrameBlackboard &GetBlackboard() const noexcept
    {
        return m_Blackboard;
    }
    RGResourcePool &GetTransientPool() noexcept
    {
        return m_TransientPool;
    }
    nvrhi::BufferHandle GetOrCreateUploadBuffer(const std::string &name, uint32_t byteSize,
                                                uint32_t versions);
    std::vector<std::unique_ptr<RenderView>> &GetViews() noexcept
    {
        return m_Views;
    }
    const std::vector<std::unique_ptr<RenderView>> &GetViews() const noexcept
    {
        return m_Views;
    }
    FrameDiagnostics &GetDiagnostics() noexcept
    {
        return m_Diagnostics;
    }
    const FrameDiagnostics &GetDiagnostics() const noexcept
    {
        return m_Diagnostics;
    }
    void BeginGpuTiming(nvrhi::ICommandList *commandList);
    void EndGpuTiming(nvrhi::ICommandList *commandList);
    GpuFrameTiming GetGpuFrameTiming() const noexcept
    {
        GpuFrameTiming timing = m_LastGpuTiming;
        timing.Pending = m_ActiveGpuQuery != InvalidGpuQueryIndex;
        for (const GpuQuerySlot &slot : m_GpuQueries)
            timing.Pending = timing.Pending || slot.Pending;
        return timing;
    }

  private:
    struct InFlightFrame
    {
        uint64_t Frame = 0;
        nvrhi::EventQueryHandle CompletionQuery;
    };
    struct UploadBuffer
    {
        nvrhi::BufferHandle Handle;
        uint32_t ByteSize = 0;
        uint32_t Versions = 0;
    };
    struct GpuQuerySlot
    {
        nvrhi::TimerQueryHandle Query;
        uint64_t FrameIndex = 0;
        bool Pending = false;
    };
    static constexpr size_t GpuQuerySlotCount = 4;
    static constexpr size_t InvalidGpuQueryIndex = GpuQuerySlotCount;

    nvrhi::DeviceHandle m_Device;
    FrameBlackboard m_Blackboard;
    RGResourcePool m_TransientPool;
    std::vector<std::unique_ptr<RenderView>> m_Views;
    std::unordered_map<std::string, UploadBuffer> m_UploadBuffers;
    std::deque<InFlightFrame> m_InFlightFrames;
    std::vector<nvrhi::EventQueryHandle> m_FreeCompletionQueries;
    FrameDiagnostics m_Diagnostics;
    std::array<GpuQuerySlot, GpuQuerySlotCount> m_GpuQueries{};
    GpuFrameTiming m_LastGpuTiming;
    size_t m_ActiveGpuQuery = InvalidGpuQueryIndex;
    uint64_t m_FrameIndex = 0;
    uint64_t m_SubmittedFrames = 0;
    uint64_t m_CompletedFrames = 0;
    bool m_CompletionQueryFailureLogged = false;
};
} // namespace Engine::Renderer

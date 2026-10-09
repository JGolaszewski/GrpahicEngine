#include "rendering/include/renderer/render_graph/FrameContext.h"
#include "runtime/include/LogMacros.h"

#include <algorithm>

namespace Engine::Renderer
{
FrameContext::FrameContext(nvrhi::IDevice *device) : m_Device(device), m_TransientPool(device)
{
}

void FrameContext::BeginFrame()
{
    m_FrameIndex = m_SubmittedFrames + 1;
    m_Blackboard.Clear();
    m_Views.clear();
    m_Diagnostics = {};
}

void FrameContext::BeginGpuTiming(nvrhi::ICommandList *commandList)
{
    if (!m_Device || !commandList || m_ActiveGpuQuery != InvalidGpuQueryIndex)
        return;

    for (GpuQuerySlot &slot : m_GpuQueries)
    {
        if (!slot.Pending || !m_Device->pollTimerQuery(slot.Query))
            continue;

        const float timeMs = m_Device->getTimerQueryTime(slot.Query);
        if (timeMs > 0.0f)
            m_LastGpuTiming = {true, false, slot.FrameIndex, timeMs};
        slot.Pending = false;
    }

    for (size_t index = 0; index < m_GpuQueries.size(); ++index)
    {
        GpuQuerySlot &slot = m_GpuQueries[index];
        if (slot.Pending)
            continue;
        if (!slot.Query)
            slot.Query = m_Device->createTimerQuery();
        if (!slot.Query)
            return;

        m_Device->resetTimerQuery(slot.Query);
        commandList->beginTimerQuery(slot.Query);
        slot.FrameIndex = m_FrameIndex - 1;
        m_ActiveGpuQuery = index;
        return;
    }
}

void FrameContext::EndGpuTiming(nvrhi::ICommandList *commandList)
{
    if (!commandList || m_ActiveGpuQuery == InvalidGpuQueryIndex)
        return;

    GpuQuerySlot &slot = m_GpuQueries[m_ActiveGpuQuery];
    commandList->endTimerQuery(slot.Query);
    slot.Pending = true;
    m_ActiveGpuQuery = InvalidGpuQueryIndex;
}

nvrhi::BufferHandle FrameContext::GetOrCreateUploadBuffer(const std::string &name, uint32_t byteSize,
                                                          uint32_t versions)
{
    if (!m_Device || name.empty() || byteSize == 0 || versions == 0)
        return nullptr;
    UploadBuffer &uploadBuffer = m_UploadBuffers[name];
    if (uploadBuffer.Handle && uploadBuffer.ByteSize >= byteSize && uploadBuffer.Versions >= versions)
    {
        return uploadBuffer.Handle;
    }
    nvrhi::BufferDesc desc;
    desc.byteSize = byteSize;
    desc.isConstantBuffer = true;
    desc.isVolatile = true;
    desc.maxVersions = versions;
    desc.debugName = name;
    nvrhi::BufferHandle replacement = m_Device->createBuffer(desc);
    if (!replacement)
        return nullptr;
    uploadBuffer.Handle = std::move(replacement);
    uploadBuffer.ByteSize = byteSize;
    uploadBuffer.Versions = versions;
    ++m_Diagnostics.BufferAllocations;
    return uploadBuffer.Handle;
}

void FrameContext::MarkSubmitted()
{
    ++m_SubmittedFrames;
    if (!m_Device)
        return;

    nvrhi::EventQueryHandle query;
    if (!m_FreeCompletionQueries.empty())
    {
        query = std::move(m_FreeCompletionQueries.back());
        m_FreeCompletionQueries.pop_back();
    }
    else
    {
        query = m_Device->createEventQuery();
    }
    if (!query)
    {
        if (!m_CompletionQueryFailureLogged)
            ENGINE_LOG_ERROR("Render", "GPU completion query creation failed | submitted_frame: {} | inflight_frames: {} | fallback: wait_for_idle",
                             m_SubmittedFrames, m_InFlightFrames.size());
        m_CompletionQueryFailureLogged = true;
        m_Device->waitForIdle();
        m_CompletedFrames = (std::max)(m_CompletedFrames, m_SubmittedFrames + FramesInFlight);
        // waitForIdle guarantees completion, but does not retrieve the query results.
        // Discard these queries instead of pooling them: the next End on a reused D3D11
        // query would abandon the previous, unread result.
        m_InFlightFrames.clear();
        return;
    }
    m_CompletionQueryFailureLogged = false;

    m_Device->setEventQuery(query, nvrhi::CommandQueue::Graphics);
    m_InFlightFrames.push_back({m_SubmittedFrames, std::move(query)});
    while (!m_InFlightFrames.empty() && m_Device->pollEventQuery(m_InFlightFrames.front().CompletionQuery))
    {
        m_CompletedFrames = (std::max)(m_CompletedFrames, m_InFlightFrames.front().Frame);
        m_Device->resetEventQuery(m_InFlightFrames.front().CompletionQuery);
        m_FreeCompletionQueries.push_back(std::move(m_InFlightFrames.front().CompletionQuery));
        m_InFlightFrames.pop_front();
    }
}
} // namespace Engine::Renderer

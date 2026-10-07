#include "renderer/render_graph/FrameContext.h"
#include "LogMacros.h"

#include <algorithm>

namespace Renderer
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
        while (!m_InFlightFrames.empty())
        {
            auto &inFlight = m_InFlightFrames.front();
            m_Device->resetEventQuery(inFlight.CompletionQuery);
            m_FreeCompletionQueries.push_back(std::move(inFlight.CompletionQuery));
            m_InFlightFrames.pop_front();
        }
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
} // namespace Renderer

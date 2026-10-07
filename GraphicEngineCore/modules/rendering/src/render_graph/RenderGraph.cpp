#include "rendering/include/renderer/render_graph/RenderGraph.h"

#include <algorithm>
#include <queue>
#include <sstream>
#include <vector>

namespace
{
bool Contains(const std::vector<::Engine::Renderer::RGResourceID> &ids, ::Engine::Renderer::RGResourceID id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void AddEdge(std::vector<std::vector<size_t>> &adjacency, std::vector<int> &inDegree, size_t from, size_t to)
{
    if (from == to || std::find(adjacency[from].begin(), adjacency[from].end(), to) != adjacency[from].end())
    {
        return;
    }
    adjacency[from].push_back(to);
    ++inDegree[to];
}

} // namespace

namespace Engine::Renderer
{
RenderGraph::RenderGraph(nvrhi::DeviceHandle device, FrameContext &frameContext)
    : m_FrameContext(frameContext), m_Device(device)
{
}

void RenderGraph::Reset()
{
    m_Registry.Clear();
    m_Nodes.clear();
    m_LastError.clear();
    m_Compiled = false;
}

bool RenderGraph::AddPass(std::unique_ptr<IRenderPass> pass)
{
    if (!pass)
    {
        m_LastError = "Cannot add a null render pass";
        return false;
    }

    RGPassDependencies deps;
    RGBuilder builder(m_Registry, deps);
    pass->Setup(builder, m_FrameContext.GetBlackboard());
    if (!deps.valid)
    {
        m_LastError = pass->GetName() + ": " + deps.error;
        return false;
    }

    RGPassNode node;
    node.pass = std::move(pass);
    node.deps = std::move(deps);
    m_Nodes.push_back(std::move(node));
    m_Compiled = false;
    return true;
}

bool RenderGraph::Compile()
{
    m_LastError.clear();
    m_Compiled = false;

    const size_t passCount = m_Nodes.size();
    if (passCount == 0)
    {
        m_Compiled = true;
        return true;
    }

    std::vector<bool> active(passCount, false);
    std::vector<bool> requiredResource(m_Registry.GetAllResources().size(), false);

    for (size_t passIndex = 0; passIndex < passCount; ++passIndex)
    {
        const auto &node = m_Nodes[passIndex];
        if (node.pass->HasSideEffect())
            active[passIndex] = true;
        const RGResourceID output = node.pass->GetOutputID();
        if (output.isValid())
        {
            if (!m_Registry.IsValid(output))
            {
                m_LastError = node.pass->GetName() + ": GetOutputID returned an invalid resource";
                return false;
            }
            requiredResource[output.value] = true;
        }
    }

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (size_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            const auto &node = m_Nodes[passIndex];
            const bool producesRequiredResource = std::any_of(
                node.deps.outputs.begin(), node.deps.outputs.end(),
                [&requiredResource](RGResourceID id) { return id.isValid() && requiredResource[id.value]; });
            if (!active[passIndex] && producesRequiredResource)
            {
                active[passIndex] = true;
                changed = true;
            }
            if (active[passIndex])
            {
                for (const RGResourceID input : node.deps.inputs)
                {
                    if (input.isValid() && !requiredResource[input.value])
                    {
                        requiredResource[input.value] = true;
                        changed = true;
                    }
                }
            }
        }
    }

    struct SubresourceHazard
    {
        int lastWriter = -1;
        std::vector<size_t> readers;
    };
    std::vector<std::vector<SubresourceHazard>> hazards;
    hazards.reserve(m_Registry.GetAllResources().size());
    for (const RGResource &resource : m_Registry.GetAllResources())
    {
        hazards.emplace_back(static_cast<size_t>(resource.desc.arraySize) * resource.desc.mipLevels);
    }
    std::vector<std::vector<size_t>> adjacency(passCount);
    std::vector<int> inDegree(passCount, 0);
    int lastSideEffectPass = -1;

    for (size_t passIndex = 0; passIndex < passCount; ++passIndex)
    {
        if (!active[passIndex])
            continue;
        const auto &node = m_Nodes[passIndex];
        if (node.pass->HasSideEffect())
        {
            if (lastSideEffectPass >= 0)
            {
                AddEdge(adjacency, inDegree, static_cast<size_t>(lastSideEffectPass), passIndex);
            }
            lastSideEffectPass = static_cast<int>(passIndex);
        }

        for (const RGTextureAccess &access : node.deps.accesses)
        {
            if (!m_Registry.IsValid(access.resource))
            {
                m_LastError = node.pass->GetName() + ": access references an invalid resource ID";
                return false;
            }
            const RGResource &resource = m_Registry.GetResource(access.resource);
            const uint32_t mipCount =
                access.subresources.numMipLevels == nvrhi::TextureSubresourceSet::AllMipLevels
                    ? resource.desc.mipLevels - access.subresources.baseMipLevel
                    : access.subresources.numMipLevels;
            const uint32_t sliceCount =
                access.subresources.numArraySlices == nvrhi::TextureSubresourceSet::AllArraySlices
                    ? resource.desc.arraySize - access.subresources.baseArraySlice
                    : access.subresources.numArraySlices;
            for (uint32_t slice = access.subresources.baseArraySlice;
                 slice < access.subresources.baseArraySlice + sliceCount; ++slice)
            {
                for (uint32_t mip = access.subresources.baseMipLevel;
                     mip < access.subresources.baseMipLevel + mipCount; ++mip)
                {
                    SubresourceHazard &hazard =
                        hazards[access.resource.value]
                               [static_cast<size_t>(slice) * resource.desc.mipLevels + mip];
                    if (access.read)
                    {
                        if (hazard.lastWriter >= 0)
                            AddEdge(adjacency, inDegree, static_cast<size_t>(hazard.lastWriter), passIndex);
                        else if (!resource.isImported)
                        {
                            m_LastError =
                                node.pass->GetName() + ": reads a transient subresource with no producer";
                            return false;
                        }
                        hazard.readers.push_back(passIndex);
                    }
                    if (access.write)
                    {
                        if (hazard.lastWriter >= 0)
                            AddEdge(adjacency, inDegree, static_cast<size_t>(hazard.lastWriter), passIndex);
                        for (const size_t reader : hazard.readers)
                            AddEdge(adjacency, inDegree, reader, passIndex);
                        hazard.readers.clear();
                        hazard.lastWriter = static_cast<int>(passIndex);
                    }
                }
            }
        }
    }

    std::queue<size_t> ready;
    for (size_t passIndex = 0; passIndex < passCount; ++passIndex)
    {
        if (active[passIndex] && inDegree[passIndex] == 0)
            ready.push(passIndex);
    }

    std::vector<size_t> sortedIndices;
    sortedIndices.reserve(passCount);
    while (!ready.empty())
    {
        const size_t passIndex = ready.front();
        ready.pop();
        sortedIndices.push_back(passIndex);
        for (const size_t dependent : adjacency[passIndex])
        {
            if (--inDegree[dependent] == 0)
                ready.push(dependent);
        }
    }

    const size_t activeCount = static_cast<size_t>(std::count(active.begin(), active.end(), true));
    if (sortedIndices.size() != activeCount)
    {
        std::ostringstream message;
        message << "Render graph cycle detected in passes: ";
        bool first = true;
        for (size_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            if (active[passIndex] && inDegree[passIndex] > 0)
            {
                if (!first)
                    message << ", ";
                message << m_Nodes[passIndex].pass->GetName();
                first = false;
            }
        }
        m_LastError = message.str();
        return false;
    }

    std::vector<RGPassNode> sortedNodes;
    sortedNodes.reserve(sortedIndices.size());
    for (const size_t passIndex : sortedIndices)
    {
        sortedNodes.push_back(std::move(m_Nodes[passIndex]));
    }
    m_Nodes = std::move(sortedNodes);

    for (RGResource &resource : m_Registry.GetAllResources())
    {
        resource.firstPassUse = UINT32_MAX;
        resource.lastPassUse = 0;
    }
    for (uint32_t passIndex = 0; passIndex < m_Nodes.size(); ++passIndex)
    {
        const auto markUse = [this, passIndex](RGResourceID id) {
            if (!m_Registry.IsValid(id))
                return;
            RGResource &resource = m_Registry.GetResource(id);
            resource.firstPassUse = (std::min)(resource.firstPassUse, passIndex);
            resource.lastPassUse = (std::max)(resource.lastPassUse, passIndex);
        };
        for (const RGResourceID id : m_Nodes[passIndex].deps.inputs)
            markUse(id);
        for (const RGResourceID id : m_Nodes[passIndex].deps.outputs)
            markUse(id);
    }

    m_Compiled = true;
    return true;
}

bool RenderGraph::Execute(nvrhi::ICommandList *cmd)
{
    if (!cmd || !m_Compiled)
    {
        m_LastError = "Cannot execute a null command list or an uncompiled graph";
        return false;
    }
    m_LastError.clear();
    FrameBlackboard &blackboard = m_FrameContext.GetBlackboard();
    const float deltaTime = blackboard.Get<float>("DeltaTime", 0.016f);
    RenderContext ctx(m_Device.Get(), &m_Registry, &m_FrameContext, deltaTime);

    for (uint32_t passIndex = 0; passIndex < m_Nodes.size(); ++passIndex)
    {
        for (RGResource &resource : m_Registry.GetAllResources())
        {
            if (resource.isImported || resource.firstPassUse != passIndex)
                continue;
            resource.physicalHandle = m_FrameContext.GetTransientPool().Acquire(resource.desc);
            if (!resource.physicalHandle)
            {
                for (RGResource &allocated : m_Registry.GetAllResources())
                {
                    if (!allocated.isImported && allocated.physicalHandle)
                    {
                        m_FrameContext.GetTransientPool().Release(allocated.desc, allocated.physicalHandle);
                        allocated.physicalHandle = nullptr;
                    }
                }
                m_LastError = "Failed to allocate a transient render graph texture";
                return false;
            }
        }
        for (RGResource &resource : m_Registry.GetAllResources())
        {
            if (resource.isImported && resource.firstPassUse == passIndex && resource.physicalHandle)
            {
                cmd->beginTrackingTextureState(resource.physicalHandle, nvrhi::AllSubresources,
                                               resource.initialState);
            }
        }
        for (const RGTextureAccess &access : m_Nodes[passIndex].deps.accesses)
        {
            RGResource &resource = m_Registry.GetResource(access.resource);
            if (!resource.physicalHandle)
                continue;
            cmd->setTextureState(resource.physicalHandle, access.subresources, access.state);
        }
        cmd->commitBarriers();

        if (!m_Nodes[passIndex].pass->Execute(cmd, ctx))
        {
            m_LastError = m_Nodes[passIndex].pass->GetName() + ": pass execution failed";
            for (RGResource &resource : m_Registry.GetAllResources())
            {
                if (!resource.isImported && resource.physicalHandle)
                {
                    m_FrameContext.GetTransientPool().Release(resource.desc, resource.physicalHandle);
                    resource.physicalHandle = nullptr;
                }
            }
            return false;
        }

        for (RGResource &resource : m_Registry.GetAllResources())
        {
            if (resource.lastPassUse == passIndex && resource.physicalHandle)
            {
                if (resource.isImported)
                {
                    cmd->setTextureState(resource.physicalHandle, nvrhi::AllSubresources,
                                         resource.finalState);
                    cmd->commitBarriers();
                }
                else
                {
                    m_FrameContext.GetTransientPool().Release(resource.desc, resource.physicalHandle);
                    resource.physicalHandle = nullptr;
                }
            }
        }
    }
    return true;
}

std::string RenderGraph::Dump() const
{
    std::ostringstream dump;
    dump << "RenderGraph\nResources:\n";
    for (uint32_t resourceIndex = 0; resourceIndex < m_Registry.GetAllResources().size(); ++resourceIndex)
    {
        const RGResource &resource = m_Registry.GetAllResources()[resourceIndex];
        dump << "  R" << resourceIndex << (resource.isImported ? " imported" : " transient") << " "
             << resource.desc.width << 'x' << resource.desc.height << " slices=" << resource.desc.arraySize
             << " mips=" << resource.desc.mipLevels
             << " format=" << static_cast<uint32_t>(resource.desc.format) << " lifetime=["
             << resource.firstPassUse << ',' << resource.lastPassUse << "]";
        if (resource.isImported)
            dump << " finalState=" << static_cast<uint32_t>(resource.finalState);
        dump << '\n';
    }
    dump << "Passes and transitions:\n";
    for (uint32_t passIndex = 0; passIndex < m_Nodes.size(); ++passIndex)
    {
        const RGPassNode &node = m_Nodes[passIndex];
        dump << "  P" << passIndex << ' ' << node.pass->GetName() << '\n';
        for (const RGTextureAccess &access : node.deps.accesses)
        {
            dump << "    R" << access.resource.value << ' '
                 << (access.read && access.write ? "read/write"
                     : access.write              ? "write"
                                                 : "read")
                 << " state=" << static_cast<uint32_t>(access.state)
                 << " mips=" << access.subresources.baseMipLevel << '+' << access.subresources.numMipLevels
                 << " slices=" << access.subresources.baseArraySlice << '+'
                 << access.subresources.numArraySlices << '\n';
        }
    }
    return dump.str();
}
} // namespace Engine::Renderer

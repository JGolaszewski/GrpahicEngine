#include "rendering/src/render_graph/RenderGraph.h"

#include <algorithm>
#include <queue>
#include <sstream>
#include <utility>
#include <vector>

namespace {
auto Contains(const std::vector<::Engine::Renderer::RGResourceID>& ids, ::Engine::Renderer::RGResourceID id) -> bool {
  return std::ranges::find(ids, id) != ids.end();
}

void AddEdge(std::vector<std::vector<size_t>>& adjacency, std::vector<int>& inDegree, size_t from, size_t to) {
  if (from == to || std::find(adjacency.at(from).begin(), adjacency.at(from).end(), to) != adjacency.at(from).end()) {
    return;
  }
  adjacency.at(from).push_back(to);
  ++inDegree.at(to);
}

struct SubresourceHazard {
  int lastWriter = -1;
  std::vector<size_t> readers;
};

auto UpdatePassActivation(const ::Engine::Renderer::RGPassNode& node, std::vector<bool>& requiredResources,
                          std::vector<bool>& activePasses, size_t passIndex) -> bool {
  bool active = activePasses.at(passIndex);
  bool changed = false;
  const bool producesRequired = std::ranges::any_of(node.deps.outputs, [&requiredResources](auto id) -> bool {
    return id.isValid() && requiredResources.at(id.value);
  });
  if (!active && producesRequired) {
    active = true;
    changed = true;
  }
  if (active) {
    for (const auto input : node.deps.inputs) {
      if (input.isValid() && !requiredResources.at(input.value)) {
        requiredResources.at(input.value) = true;
        changed = true;
      }
    }
  }
  activePasses.at(passIndex) = active;
  return changed;
}

auto MarkActivePasses(const std::vector<::Engine::Renderer::RGPassNode>& nodes,
                      const ::Engine::Renderer::RGResourceRegistry& registry, std::vector<bool>& active,
                      std::vector<bool>& requiredResources, std::string& error) -> bool {
  for (size_t passIndex = 0; passIndex < nodes.size(); ++passIndex) {
    const auto& node = nodes.at(passIndex);
    active.at(passIndex) = node.pass->HasSideEffect();
    const auto output = node.pass->GetOutputID();
    if (output.isValid()) {
      if (!registry.IsValid(output)) {
        error = node.pass->GetName() + ": GetOutputID returned an invalid resource";
        return false;
      }
      requiredResources.at(output.value) = true;
    }
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t passIndex = 0; passIndex < nodes.size(); ++passIndex) {
      changed |= UpdatePassActivation(nodes.at(passIndex), requiredResources, active, passIndex);
    }
  }
  return true;
}

auto ProcessSubresourceHazard(const ::Engine::Renderer::RGTextureAccess& access,
                              const ::Engine::Renderer::RGResource& resource, size_t passIndex,
                              SubresourceHazard& hazard, std::vector<std::vector<size_t>>& adjacency,
                              std::vector<int>& inDegree, std::string& error, const std::string& passName) -> bool {
  if (access.read) {
    if (hazard.lastWriter >= 0) {
      AddEdge(adjacency, inDegree, static_cast<size_t>(hazard.lastWriter), passIndex);
    } else if (!resource.isImported) {
      error = passName + ": reads a transient subresource with no producer";
      return false;
    }
    hazard.readers.push_back(passIndex);
  }
  if (access.write) {
    if (hazard.lastWriter >= 0) {
      AddEdge(adjacency, inDegree, static_cast<size_t>(hazard.lastWriter), passIndex);
    }
    for (const size_t reader : hazard.readers) {
      AddEdge(adjacency, inDegree, reader, passIndex);
    }
    hazard.readers.clear();
    hazard.lastWriter = static_cast<int>(passIndex);
  }
  return true;
}

auto ProcessTextureAccess(const ::Engine::Renderer::RGTextureAccess& access,
                          const ::Engine::Renderer::RGResource& resource, size_t passIndex,
                          std::vector<SubresourceHazard>& hazards, std::vector<std::vector<size_t>>& adjacency,
                          std::vector<int>& inDegree, std::string& error, const std::string& passName) -> bool {
  const uint32_t mipCount = access.subresources.numMipLevels == nvrhi::TextureSubresourceSet::AllMipLevels
                                ? resource.desc.mipLevels - access.subresources.baseMipLevel
                                : access.subresources.numMipLevels;
  const uint32_t sliceCount = access.subresources.numArraySlices == nvrhi::TextureSubresourceSet::AllArraySlices
                                  ? resource.desc.arraySize - access.subresources.baseArraySlice
                                  : access.subresources.numArraySlices;
  for (uint32_t slice = access.subresources.baseArraySlice; slice < access.subresources.baseArraySlice + sliceCount;
       ++slice) {
    for (uint32_t mip = access.subresources.baseMipLevel; mip < access.subresources.baseMipLevel + mipCount; ++mip) {
      auto& hazard = hazards.at((static_cast<size_t>(slice) * resource.desc.mipLevels) + mip);
      if (!ProcessSubresourceHazard(access, resource, passIndex, hazard, adjacency, inDegree, error, passName)) {
        return false;
      }
    }
  }
  return true;
}

auto BuildDependencyEdges(const std::vector<::Engine::Renderer::RGPassNode>& nodes,
                          ::Engine::Renderer::RGResourceRegistry& registry, const std::vector<bool>& active,
                          std::vector<std::vector<SubresourceHazard>>& hazards,
                          std::vector<std::vector<size_t>>& adjacency, std::vector<int>& inDegree, std::string& error)
    -> bool {
  int lastSideEffectPass = -1;
  for (size_t passIndex = 0; passIndex < nodes.size(); ++passIndex) {
    if (!active.at(passIndex)) {
      continue;
    }
    const auto& node = nodes.at(passIndex);
    if (node.pass->HasSideEffect()) {
      if (lastSideEffectPass >= 0) {
        AddEdge(adjacency, inDegree, static_cast<size_t>(lastSideEffectPass), passIndex);
      }
      lastSideEffectPass = static_cast<int>(passIndex);
    }
    for (const auto& access : node.deps.accesses) {
      if (!registry.IsValid(access.resource)) {
        error = node.pass->GetName() + ": access references an invalid resource ID";
        return false;
      }
      const auto& resource = registry.GetResource(access.resource);
      if (!ProcessTextureAccess(access, resource, passIndex, hazards.at(access.resource.value), adjacency, inDegree,
                                error, node.pass->GetName())) {
        return false;
      }
    }
  }
  return true;
}

auto BuildTopologicalOrder(const std::vector<::Engine::Renderer::RGPassNode>& nodes, const std::vector<bool>& active,
                           std::vector<std::vector<size_t>>& adjacency, std::vector<int>& inDegree,
                           std::vector<size_t>& sortedIndices, std::string& error) -> bool {
  std::queue<size_t> ready;
  for (size_t passIndex = 0; passIndex < nodes.size(); ++passIndex) {
    if (active.at(passIndex) && inDegree.at(passIndex) == 0) {
      ready.push(passIndex);
    }
  }
  sortedIndices.reserve(nodes.size());
  while (!ready.empty()) {
    const size_t passIndex = ready.front();
    ready.pop();
    sortedIndices.push_back(passIndex);
    for (const size_t dependent : adjacency.at(passIndex)) {
      if (--inDegree.at(dependent) == 0) {
        ready.push(dependent);
      }
    }
  }
  const size_t activeCount = static_cast<size_t>(std::count(active.begin(), active.end(), true));
  if (sortedIndices.size() == activeCount) {
    return true;
  }
  std::ostringstream message;
  message << "Render graph cycle detected in passes: ";
  bool first = true;
  for (size_t passIndex = 0; passIndex < nodes.size(); ++passIndex) {
    if (active.at(passIndex) && inDegree.at(passIndex) > 0) {
      if (!first) {
        message << ", ";
      }
      message << nodes.at(passIndex).pass->GetName();
      first = false;
    }
  }
  error = message.str();
  return false;
}

void MarkResourceLifetimes(std::vector<::Engine::Renderer::RGPassNode>& nodes,
                           ::Engine::Renderer::RGResourceRegistry& registry) {
  for (auto& resource : registry.GetAllResources()) {
    resource.firstPassUse = UINT32_MAX;
    resource.lastPassUse = 0;
  }
  for (uint32_t passIndex = 0; passIndex < nodes.size(); ++passIndex) {
    const auto markUse = [&registry, passIndex](auto id) -> void {
      if (!registry.IsValid(id)) {
        return;
      }
      auto& resource = registry.GetResource(id);
      resource.firstPassUse = (std::min)(resource.firstPassUse, passIndex);
      resource.lastPassUse = (std::max)(resource.lastPassUse, passIndex);
    };
    for (const auto id : nodes.at(passIndex).deps.inputs) {
      markUse(id);
    }
    for (const auto id : nodes.at(passIndex).deps.outputs) {
      markUse(id);
    }
  }
}

} // namespace

namespace Engine::Renderer {
RenderGraph::RenderGraph(nvrhi::DeviceHandle device, FrameContext& frameContext)
    : m_FrameContext(frameContext), m_Device(std::move(device)) {}

void RenderGraph::Reset() {
  m_Registry.Clear();
  m_Nodes.clear();
  m_LastError.clear();
  m_Compiled = false;
}

auto RenderGraph::AddPass(std::unique_ptr<IRenderPass> pass) -> bool {
  if (!pass) {
    m_LastError = "Cannot add a null render pass";
    return false;
  }

  RGPassDependencies deps;
  RGBuilder builder(m_Registry, deps);
  pass->Setup(builder, m_FrameContext.GetBlackboard());
  if (!deps.valid) {
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

auto RenderGraph::Compile() -> bool {
  m_LastError.clear();
  m_Compiled = false;

  const size_t passCount = m_Nodes.size();
  if (passCount == 0) {
    m_Compiled = true;
    return true;
  }

  std::vector<bool> active(passCount, false);
  std::vector<bool> requiredResources(m_Registry.GetAllResources().size(), false);
  if (!MarkActivePasses(m_Nodes, m_Registry, active, requiredResources, m_LastError)) {
    return false;
  }

  std::vector<std::vector<SubresourceHazard>> hazards;
  hazards.reserve(m_Registry.GetAllResources().size());
  for (const RGResource& resource : m_Registry.GetAllResources()) {
    hazards.emplace_back(static_cast<size_t>(resource.desc.arraySize) * resource.desc.mipLevels);
  }
  std::vector<std::vector<size_t>> adjacency(passCount);
  std::vector<int> inDegree(passCount, 0);
  if (!BuildDependencyEdges(m_Nodes, m_Registry, active, hazards, adjacency, inDegree, m_LastError)) {
    return false;
  }

  std::vector<size_t> sortedIndices;
  if (!BuildTopologicalOrder(m_Nodes, active, adjacency, inDegree, sortedIndices, m_LastError)) {
    return false;
  }

  std::vector<RGPassNode> sortedNodes;
  sortedNodes.reserve(sortedIndices.size());
  for (const size_t passIndex : sortedIndices) {
    sortedNodes.push_back(std::move(m_Nodes.at(passIndex)));
  }
  m_Nodes = std::move(sortedNodes);
  MarkResourceLifetimes(m_Nodes, m_Registry);

  m_Compiled = true;
  return true;
}

auto RenderGraph::AcquireTransientResources(uint32_t passIndex) -> bool {
  for (RGResource& resource : m_Registry.GetAllResources()) {
    if (resource.isImported || resource.firstPassUse != passIndex) {
      continue;
    }
    resource.physicalHandle = m_FrameContext.GetTransientPool().Acquire(resource.desc);
    if (resource.physicalHandle == nullptr) {
      ReleaseTransientResources();
      m_LastError = "Failed to allocate a transient render graph texture";
      return false;
    }
  }
  return true;
}

void RenderGraph::TrackImportedResources(uint32_t passIndex, nvrhi::ICommandList* commandList) {
  for (RGResource& resource : m_Registry.GetAllResources()) {
    if (resource.isImported && resource.firstPassUse == passIndex && (resource.physicalHandle != nullptr)) {
      commandList->beginTrackingTextureState(resource.physicalHandle, nvrhi::AllSubresources, resource.initialState);
    }
  }
}

void RenderGraph::ApplyAccessStates(uint32_t passIndex, nvrhi::ICommandList* commandList) {
  for (const RGTextureAccess& access : m_Nodes.at(passIndex).deps.accesses) {
    RGResource& resource = m_Registry.GetResource(access.resource);
    if (resource.physicalHandle != nullptr) {
      commandList->setTextureState(resource.physicalHandle, access.subresources, access.state);
    }
  }
}

void RenderGraph::ReleaseTransientResources() {
  for (RGResource& resource : m_Registry.GetAllResources()) {
    if (!resource.isImported && (resource.physicalHandle != nullptr)) {
      m_FrameContext.GetTransientPool().Release(resource.desc, resource.physicalHandle);
      resource.physicalHandle = nullptr;
    }
  }
}

void RenderGraph::ReleaseResourcesAtPassEnd(uint32_t passIndex, nvrhi::ICommandList* commandList) {
  for (RGResource& resource : m_Registry.GetAllResources()) {
    if (resource.lastPassUse != passIndex || (resource.physicalHandle == nullptr)) {
      continue;
    }
    if (resource.isImported) {
      commandList->setTextureState(resource.physicalHandle, nvrhi::AllSubresources, resource.finalState);
      commandList->commitBarriers();
    } else {
      m_FrameContext.GetTransientPool().Release(resource.desc, resource.physicalHandle);
      resource.physicalHandle = nullptr;
    }
  }
}

auto RenderGraph::Execute(nvrhi::ICommandList* cmd) -> bool {
  if ((cmd == nullptr) || !m_Compiled) {
    m_LastError = "Cannot execute a null command list or an uncompiled graph";
    return false;
  }
  m_LastError.clear();
  FrameBlackboard& blackboard = m_FrameContext.GetBlackboard();
  const float deltaTime = blackboard.Get<float>("DeltaTime", 0.016F);
  RenderContext context(m_Device.Get(), &m_Registry, &m_FrameContext, deltaTime);

  for (uint32_t passIndex = 0; passIndex < m_Nodes.size(); ++passIndex) {
    if (!AcquireTransientResources(passIndex)) {
      return false;
    }
    TrackImportedResources(passIndex, cmd);
    ApplyAccessStates(passIndex, cmd);
    cmd->commitBarriers();
    if (!m_Nodes.at(passIndex).pass->Execute(cmd, context)) {
      m_LastError = m_Nodes.at(passIndex).pass->GetName() + ": pass execution failed";
      ReleaseTransientResources();
      return false;
    }
    ReleaseResourcesAtPassEnd(passIndex, cmd);
  }
  return true;
}

auto RenderGraph::Dump() const -> std::string {
  std::ostringstream dump;
  dump << "RenderGraph\nResources:\n";
  for (uint32_t resourceIndex = 0; resourceIndex < m_Registry.GetAllResources().size(); ++resourceIndex) {
    const RGResource& resource = m_Registry.GetAllResources().subspan(resourceIndex, 1).front();
    dump << "  R" << resourceIndex << (resource.isImported ? " imported" : " transient") << " " << resource.desc.width
         << 'x' << resource.desc.height << " slices=" << resource.desc.arraySize << " mips=" << resource.desc.mipLevels
         << " format=" << static_cast<uint32_t>(resource.desc.format) << " lifetime=[" << resource.firstPassUse << ','
         << resource.lastPassUse << "]";
    if (resource.isImported) {
      dump << " finalState=" << static_cast<uint32_t>(resource.finalState);
    }
    dump << '\n';
  }
  dump << "Passes and transitions:\n";
  for (uint32_t passIndex = 0; passIndex < m_Nodes.size(); ++passIndex) {
    const RGPassNode& node = m_Nodes.at(passIndex);
    dump << "  P" << passIndex << ' ' << node.pass->GetName() << '\n';
    for (const RGTextureAccess& access : node.deps.accesses) {
      const char* accessMode = "read";
      if (access.read && access.write) {
        accessMode = "read/write";
      } else if (access.write) {
        accessMode = "write";
      }
      dump << "    R" << access.resource.value << ' ' << accessMode << " state=" << static_cast<uint32_t>(access.state)
           << " mips=" << access.subresources.baseMipLevel << '+' << access.subresources.numMipLevels
           << " slices=" << access.subresources.baseArraySlice << '+' << access.subresources.numArraySlices << '\n';
    }
  }
  return dump.str();
}
} // namespace Engine::Renderer

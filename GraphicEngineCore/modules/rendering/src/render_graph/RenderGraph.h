#pragma once

#include <memory>
#include <nvrhi/nvrhi.h>
#include <string>
#include <vector>

#include "rendering/src/render_graph/FrameBlackboard.h"
#include "rendering/src/render_graph/FrameContext.h"
#include "rendering/src/render_graph/RGBuilder.h"
#include "rendering/src/render_graph/RGResourcePool.h"
#include "rendering/src/render_graph/RGResources.h"
#include "rendering/src/render_graph/RenderContext.h"
#include "rendering/src/render_graph/RenderPass.h"
#include "runtime/include/CoreExport.h"

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Renderer {

struct RGPassNode {
  std::unique_ptr<IRenderPass> pass;
  RGPassDependencies deps;

  RGPassNode() = default;

  // Block copying
  RGPassNode(const RGPassNode&) = delete;
  RGPassNode& operator=(const RGPassNode&) = delete;

  // Move allow
  RGPassNode(RGPassNode&&) noexcept = default;
  RGPassNode& operator=(RGPassNode&&) noexcept = default;
};

class ENGINE_API RenderGraph {
private:
  bool AcquireTransientResources(uint32_t passIndex);
  void TrackImportedResources(uint32_t passIndex, nvrhi::ICommandList* commandList);
  void ApplyAccessStates(uint32_t passIndex, nvrhi::ICommandList* commandList);
  void ReleaseTransientResources();
  void ReleaseResourcesAtPassEnd(uint32_t passIndex, nvrhi::ICommandList* commandList);

  RGResourceRegistry m_Registry;
  FrameContext& m_FrameContext;
  nvrhi::DeviceHandle m_Device;
  std::vector<RGPassNode> m_Nodes;
  std::string m_LastError;
  bool m_Compiled = false;

public:
  RenderGraph(nvrhi::DeviceHandle device, FrameContext& frameContext);

  FrameBlackboard& GetBlackboard() { return m_FrameContext.GetBlackboard(); }

  void Reset();
  bool AddPass(std::unique_ptr<IRenderPass> pass);
  bool Compile();
  bool Execute(nvrhi::ICommandList* cmd);
  const std::string& GetLastError() const noexcept { return m_LastError; }
  std::string Dump() const;

  RenderGraph(const RenderGraph&) = delete;
  RenderGraph& operator=(const RenderGraph&) = delete;

  RenderGraph(RenderGraph&&) noexcept = default;
  RenderGraph& operator=(RenderGraph&&) noexcept = default;
};

} // namespace Engine::Renderer

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

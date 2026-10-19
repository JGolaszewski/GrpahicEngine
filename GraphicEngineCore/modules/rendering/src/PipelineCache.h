#pragma once

#include "rendering/include/renderer/Mesh.h"
#include <nvrhi/nvrhi.h>
#include <unordered_map>
#include <vector>

namespace Engine {

struct PipelineKey {
  nvrhi::ShaderHandle VS;
  nvrhi::ShaderHandle PS;
  nvrhi::BindingLayoutHandle PerObjectLayout;
  nvrhi::BindingLayoutHandle SceneLayout;
  nvrhi::BindingLayoutHandle MaterialLayout;
  nvrhi::BindingLayoutHandle AdditionalLayout;
  bool Wireframe = false;
  bool DepthTest = true;
  bool DepthWrite = true;
  nvrhi::RasterCullMode CullMode = nvrhi::RasterCullMode::Back;

  nvrhi::FramebufferInfo Framebuffer;
  nvrhi::PrimitiveType PrimType = nvrhi::PrimitiveType::TriangleList;
  bool PrimitiveRestart = false;
  std::vector<nvrhi::VertexAttributeDesc> InputLayout;
  nvrhi::BlendState BlendState;
  nvrhi::DepthStencilState DepthStencilState;
  nvrhi::RasterState RasterState;
  nvrhi::SinglePassStereoState SinglePassStereoState;

  bool operator==(const PipelineKey& other) const;
};

struct PipelineKeyHasher {
  std::size_t operator()(const PipelineKey& k) const;
};

class PipelineCache {
public:
  struct FrameStats {
    uint32_t Hits = 0;
    uint32_t Misses = 0;
    uint32_t Creations = 0;
  };

  void Init(nvrhi::IDevice* device);
  void BeginFrame() noexcept { m_FrameStats = {}; }
  const FrameStats& GetFrameStats() const noexcept { return m_FrameStats; }

  nvrhi::GraphicsPipelineHandle GetOrCreatePipeline(const PipelineKey& key, const nvrhi::FramebufferInfo& fbInfo);

private:
  nvrhi::DeviceHandle m_Device;
  std::unordered_map<PipelineKey, nvrhi::GraphicsPipelineHandle, PipelineKeyHasher> m_Cache;
  FrameStats m_FrameStats;
};
} // namespace Engine

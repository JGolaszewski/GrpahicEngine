#pragma once

#include <DirectXMath.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <nvrhi/nvrhi.h>

#include "rendering/src/PipelineCache.h"

namespace Engine::Renderer {

struct PerObjectGpuData {
  DirectX::XMFLOAT4X4 MVP;
  DirectX::XMFLOAT4X4 World;
  DirectX::XMFLOAT4X4 Normal;
  DirectX::XMFLOAT4 ObjectParams;
  uint32_t MaterialInstanceId = 0;
  std::array<uint8_t, 44> Padding{};
};
static_assert(sizeof(PerObjectGpuData) == 256);
static_assert(offsetof(PerObjectGpuData, MaterialInstanceId) == 208);

enum class RenderQueueType { Opaque, Masked, Transparent, ShadowCaster, Skybox, UI };

struct RenderPacket {
  nvrhi::GraphicsPipelineHandle Pipeline;
  nvrhi::GraphicsPipelineHandle OverdrawPipeline;
  nvrhi::BindingSetHandle MaterialBindings;
  PerObjectGpuData PerObjectData{};
  nvrhi::BufferHandle VertexBuffer;
  nvrhi::BufferHandle IndexBuffer;
  uint32_t IndexCount;
  uint32_t VertexCount;
  uint32_t BaseVertexOffset;
  uint32_t StartIndexLocation;
  RenderQueueType Queue;
  float CameraDepth = 0.0f;
  bool ShadowAlphaTest = false;
};
} // namespace Engine::Renderer

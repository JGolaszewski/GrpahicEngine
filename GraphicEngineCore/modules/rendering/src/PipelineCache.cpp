#include "rendering/include/PipelineCache.h"
#include "runtime/include/LogMacros.h"

#include <bit>
#include <cstdint>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>

namespace {
template <typename T> void HashCombine(std::size_t& seed, const T& value) {
  seed ^= std::hash<T>{}(value) + static_cast<std::size_t>(0x9e3779b9) + (seed << 6) + (seed >> 2);
}

template <typename T> void HashEnum(std::size_t& seed, T value) {
  HashCombine(seed, static_cast<std::underlying_type_t<T>>(value));
}

void HashFloat(std::size_t& seed, float value) { HashCombine(seed, std::bit_cast<uint32_t>(value)); }

auto Equal(const nvrhi::VertexAttributeDesc& a, const nvrhi::VertexAttributeDesc& b) -> bool {
  return a.name == b.name && a.format == b.format && a.arraySize == b.arraySize && a.bufferIndex == b.bufferIndex &&
         a.offset == b.offset && a.elementStride == b.elementStride && a.isInstanced == b.isInstanced;
}

void Hash(std::size_t& seed, const nvrhi::VertexAttributeDesc& attribute) {
  HashCombine(seed, attribute.name);
  HashEnum(seed, attribute.format);
  HashCombine(seed, attribute.arraySize);
  HashCombine(seed, attribute.bufferIndex);
  HashCombine(seed, attribute.offset);
  HashCombine(seed, attribute.elementStride);
  HashCombine(seed, attribute.isInstanced);
}

auto Equal(const nvrhi::DepthStencilState::StencilOpDesc& a, const nvrhi::DepthStencilState::StencilOpDesc& b) -> bool {
  return a.failOp == b.failOp && a.depthFailOp == b.depthFailOp && a.passOp == b.passOp &&
         a.stencilFunc == b.stencilFunc;
}

void Hash(std::size_t& seed, const nvrhi::DepthStencilState::StencilOpDesc& state) {
  HashEnum(seed, state.failOp);
  HashEnum(seed, state.depthFailOp);
  HashEnum(seed, state.passOp);
  HashEnum(seed, state.stencilFunc);
}

auto Equal(const nvrhi::DepthStencilState& a, const nvrhi::DepthStencilState& b) -> bool {
  return a.depthTestEnable == b.depthTestEnable && a.depthWriteEnable == b.depthWriteEnable &&
         a.depthFunc == b.depthFunc && a.stencilEnable == b.stencilEnable && a.stencilReadMask == b.stencilReadMask &&
         a.stencilWriteMask == b.stencilWriteMask && a.stencilRefValue == b.stencilRefValue &&
         a.dynamicStencilRef == b.dynamicStencilRef && Equal(a.frontFaceStencil, b.frontFaceStencil) &&
         Equal(a.backFaceStencil, b.backFaceStencil);
}

void Hash(std::size_t& seed, const nvrhi::DepthStencilState& state) {
  HashCombine(seed, state.depthTestEnable);
  HashCombine(seed, state.depthWriteEnable);
  HashEnum(seed, state.depthFunc);
  HashCombine(seed, state.stencilEnable);
  HashCombine(seed, state.stencilReadMask);
  HashCombine(seed, state.stencilWriteMask);
  HashCombine(seed, state.stencilRefValue);
  HashCombine(seed, state.dynamicStencilRef);
  Hash(seed, state.frontFaceStencil);
  Hash(seed, state.backFaceStencil);
}

auto Equal(const nvrhi::RasterState& a, const nvrhi::RasterState& b) -> bool {
  if (a.fillMode != b.fillMode || a.cullMode != b.cullMode || a.frontCounterClockwise != b.frontCounterClockwise ||
      a.depthClipEnable != b.depthClipEnable || a.scissorEnable != b.scissorEnable ||
      a.multisampleEnable != b.multisampleEnable || a.antialiasedLineEnable != b.antialiasedLineEnable ||
      a.depthBias != b.depthBias || a.depthBiasClamp != b.depthBiasClamp ||
      a.slopeScaledDepthBias != b.slopeScaledDepthBias || a.forcedSampleCount != b.forcedSampleCount ||
      a.programmableSamplePositionsEnable != b.programmableSamplePositionsEnable ||
      a.conservativeRasterEnable != b.conservativeRasterEnable || a.quadFillEnable != b.quadFillEnable) {
    return false;
  }
  for (size_t i = 0; i < std::size(a.samplePositionsX); ++i) {
    if (a.samplePositionsX[i] != b.samplePositionsX[i] || a.samplePositionsY[i] != b.samplePositionsY[i]) {
      return false;
    }
  }
  return true;
}

void Hash(std::size_t& seed, const nvrhi::RasterState& state) {
  HashEnum(seed, state.fillMode);
  HashEnum(seed, state.cullMode);
  HashCombine(seed, state.frontCounterClockwise);
  HashCombine(seed, state.depthClipEnable);
  HashCombine(seed, state.scissorEnable);
  HashCombine(seed, state.multisampleEnable);
  HashCombine(seed, state.antialiasedLineEnable);
  HashCombine(seed, state.depthBias);
  HashFloat(seed, state.depthBiasClamp);
  HashFloat(seed, state.slopeScaledDepthBias);
  HashCombine(seed, state.forcedSampleCount);
  HashCombine(seed, state.programmableSamplePositionsEnable);
  HashCombine(seed, state.conservativeRasterEnable);
  HashCombine(seed, state.quadFillEnable);
  for (size_t i = 0; i < std::size(state.samplePositionsX); ++i) {
    HashCombine(seed, state.samplePositionsX[i]);
    HashCombine(seed, state.samplePositionsY[i]);
  }
}
} // namespace

auto Engine::PipelineKey::operator==(const PipelineKey& other) const -> bool {
  if (VS != other.VS || PS != other.PS || PerObjectLayout != other.PerObjectLayout ||
      SceneLayout != other.SceneLayout || MaterialLayout != other.MaterialLayout || Wireframe != other.Wireframe ||
      DepthTest != other.DepthTest || DepthWrite != other.DepthWrite || CullMode != other.CullMode ||
      Framebuffer != other.Framebuffer || PrimType != other.PrimType || PrimitiveRestart != other.PrimitiveRestart ||
      BlendState != other.BlendState || !Equal(DepthStencilState, other.DepthStencilState) ||
      !Equal(RasterState, other.RasterState) || SinglePassStereoState != other.SinglePassStereoState ||
      InputLayout.size() != other.InputLayout.size()) {
    return false;
  }

  for (size_t i = 0; i < InputLayout.size(); ++i) {
    if (!Equal(InputLayout.at(i), other.InputLayout.at(i))) {
      return false;
    }
  }
  return true;
}

auto Engine::PipelineKeyHasher::operator()(const PipelineKey& key) const -> std::size_t {
  std::size_t result = 0;
  HashCombine(result, key.VS.Get());
  HashCombine(result, key.PS.Get());
  HashCombine(result, key.PerObjectLayout.Get());
  HashCombine(result, key.SceneLayout.Get());
  HashCombine(result, key.MaterialLayout.Get());
  HashCombine(result, key.Wireframe);
  HashCombine(result, key.DepthTest);
  HashCombine(result, key.DepthWrite);
  HashEnum(result, key.CullMode);
  for (const auto format : key.Framebuffer.colorFormats) {
    HashEnum(result, format);
  }
  HashEnum(result, key.Framebuffer.depthFormat);
  HashCombine(result, key.Framebuffer.sampleCount);
  HashCombine(result, key.Framebuffer.sampleQuality);
  HashEnum(result, key.PrimType);
  HashCombine(result, key.PrimitiveRestart);
  for (const auto& attribute : key.InputLayout) {
    Hash(result, attribute);
  }

  for (auto target : key.BlendState.targets) {
    HashCombine(result, target.blendEnable);
    HashEnum(result, target.srcBlend);
    HashEnum(result, target.destBlend);
    HashEnum(result, target.blendOp);
    HashEnum(result, target.srcBlendAlpha);
    HashEnum(result, target.destBlendAlpha);
    HashEnum(result, target.blendOpAlpha);
    HashEnum(result, target.colorWriteMask);
  }
  HashCombine(result, key.BlendState.alphaToCoverageEnable);
  Hash(result, key.DepthStencilState);
  Hash(result, key.RasterState);
  HashCombine(result, key.SinglePassStereoState.enabled);
  HashCombine(result, key.SinglePassStereoState.independentViewportMask);
  HashCombine(result, key.SinglePassStereoState.renderTargetIndexOffset);
  return result;
}

void Engine::PipelineCache::Init(nvrhi::IDevice* device) {
  m_Device = device;
  m_Cache.clear();
}

auto Engine::PipelineCache::GetOrCreatePipeline(const PipelineKey& key, const nvrhi::FramebufferInfo& fbInfo)
    -> nvrhi::GraphicsPipelineHandle {
  PipelineKey effectiveKey = key;
  effectiveKey.Framebuffer = fbInfo;
  if (effectiveKey.InputLayout.empty()) {
    effectiveKey.InputLayout = ::Engine::Renderer::Mesh::GetVertexAttributes();
  }

  auto it = m_Cache.find(effectiveKey);
  if (it != m_Cache.end()) {
    ++m_FrameStats.Hits;
    return it->second;
  }
  ++m_FrameStats.Misses;

  if (m_Device == nullptr) {
    ENGINE_LOG_ERROR("Render", "Pipeline creation rejected | device_valid: false | cache_size: {}", m_Cache.size());
    return nullptr;
  }
  if (effectiveKey.VS == nullptr) {
    ENGINE_LOG_ERROR("Render", "Pipeline creation rejected | vertex_shader_valid: false | cache_size: {}",
                     m_Cache.size());
    return nullptr;
  }

  auto inputLayout = m_Device->createInputLayout(
      effectiveKey.InputLayout.data(), static_cast<uint32_t>(effectiveKey.InputLayout.size()), effectiveKey.VS);
  if (inputLayout == nullptr) {
    ENGINE_LOG_ERROR("Render", "Pipeline input-layout creation failed | attribute_count: {}",
                     effectiveKey.InputLayout.size());
    return nullptr;
  }

  nvrhi::GraphicsPipelineDesc pdesc;
  pdesc.primType = effectiveKey.PrimType;
  pdesc.VS = effectiveKey.VS;
  pdesc.PS = effectiveKey.PS;
  pdesc.inputLayout = inputLayout;
  pdesc.bindingLayouts = {effectiveKey.PerObjectLayout};
  if (effectiveKey.SceneLayout != nullptr) {
    pdesc.bindingLayouts.push_back(effectiveKey.SceneLayout);
  }
  if (effectiveKey.MaterialLayout != nullptr) {
    pdesc.bindingLayouts.push_back(effectiveKey.MaterialLayout);
  }

  pdesc.renderState.blendState = effectiveKey.BlendState;
  pdesc.renderState.depthStencilState = effectiveKey.DepthStencilState;
  pdesc.renderState.depthStencilState.depthTestEnable = effectiveKey.DepthTest;
  pdesc.renderState.depthStencilState.depthWriteEnable = effectiveKey.DepthWrite;
  pdesc.renderState.rasterState = effectiveKey.RasterState;
  pdesc.renderState.rasterState.fillMode =
      effectiveKey.Wireframe ? nvrhi::RasterFillMode::Wireframe : nvrhi::RasterFillMode::Solid;
  pdesc.renderState.rasterState.cullMode = effectiveKey.CullMode;
  pdesc.renderState.singlePassStereo = effectiveKey.SinglePassStereoState;

  auto pipeline = m_Device->createGraphicsPipeline(pdesc, effectiveKey.Framebuffer);
  if (pipeline == nullptr) {
    ENGINE_LOG_ERROR("Render", "Graphics pipeline creation failed | cache_size: {} | color_target_count: {}",
                     m_Cache.size(), effectiveKey.Framebuffer.colorFormats.size());
    return nullptr;
  }

  const size_t colorTargetCount = effectiveKey.Framebuffer.colorFormats.size();
  m_Cache.emplace(std::move(effectiveKey), pipeline);
  ++m_FrameStats.Creations;
  ENGINE_LOG_DEBUG("Render", "Graphics pipeline cached | cache_size: {} | color_target_count: {}", m_Cache.size(),
                   colorTargetCount);
  return pipeline;
}

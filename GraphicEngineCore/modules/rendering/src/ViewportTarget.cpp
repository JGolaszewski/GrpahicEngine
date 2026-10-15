#include "rendering/include/ViewportTarget.h"

#include <nvrhi/nvrhi.h>

#include <array>

namespace Engine {

struct ViewportTarget::Impl {
  uint32_t Width = 0;
  uint32_t Height = 0;

  nvrhi::TextureHandle ColorTexture;
  std::vector<nvrhi::TextureHandle> OutputTextures;
  nvrhi::TextureHandle DepthTexture;
  nvrhi::FramebufferHandle Framebuffer;
};

ViewportTarget::ViewportTarget(void* device, uint32_t width, uint32_t height, bool finalOutput)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Width = width;
  m_Impl->Height = height;

  if ((device == nullptr) || width == 0 || height == 0) {
    return;
  }

  auto* nvrhiDevice = static_cast<nvrhi::IDevice*>(device);

  if (finalOutput) {
    constexpr std::array<nvrhi::Format, 5> outputFormats = {nvrhi::Format::RGBA32_FLOAT, nvrhi::Format::R32_FLOAT,
                                                            nvrhi::Format::RGBA32_FLOAT, nvrhi::Format::RGBA32_FLOAT,
                                                            nvrhi::Format::RGBA32_FLOAT};
    constexpr std::array<const char*, 5> outputNames = {"FinalColor", "Depth", "WorldNormal", "SpecularLighting",
                                                        "DiffuseLighting"};
    m_Impl->OutputTextures.reserve(outputFormats.size());
    for (size_t index = 0; index < outputFormats.size(); ++index) {
      nvrhi::TextureDesc outputDesc;
      outputDesc.width = width;
      outputDesc.height = height;
      outputDesc.format = outputFormats.at(index);
      outputDesc.isRenderTarget = true;
      outputDesc.isShaderResource = true;
      outputDesc.initialState = nvrhi::ResourceStates::RenderTarget;
      outputDesc.keepInitialState = true;
      outputDesc.debugName = outputNames.at(index);
      auto texture = nvrhiDevice->createTexture(outputDesc);
      if (texture == nullptr) {
        return;
      }
      m_Impl->OutputTextures.push_back(std::move(texture));
    }
    m_Impl->ColorTexture = m_Impl->OutputTextures.front();
  } else {
    nvrhi::TextureDesc colorDesc;
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.format = nvrhi::Format::RGBA8_UNORM;
    colorDesc.isRenderTarget = true;
    colorDesc.isShaderResource = true;
    colorDesc.initialState = nvrhi::ResourceStates::RenderTarget;
    colorDesc.keepInitialState = true;
    colorDesc.debugName = "ViewportColor";
    m_Impl->ColorTexture = nvrhiDevice->createTexture(colorDesc);
    if (m_Impl->ColorTexture == nullptr) {
      return;
    }
    m_Impl->OutputTextures.push_back(m_Impl->ColorTexture);
  }

  nvrhi::TextureDesc depthDesc;
  depthDesc.width = width;
  depthDesc.height = height;
  depthDesc.depth = 1;
  depthDesc.arraySize = 1;
  depthDesc.mipLevels = 1;
  depthDesc.format = nvrhi::Format::D24S8;
  depthDesc.sampleCount = 1;
  depthDesc.sampleQuality = 0;
  depthDesc.isRenderTarget = true;
  depthDesc.isUAV = false;
  depthDesc.isShaderResource = false;
  depthDesc.initialState = nvrhi::ResourceStates::DepthWrite;
  depthDesc.keepInitialState = true;
  depthDesc.debugName = "ViewportDepth";
  m_Impl->DepthTexture = nvrhiDevice->createTexture(depthDesc);
  if (m_Impl->DepthTexture == nullptr) {
    return;
  }

  nvrhi::FramebufferDesc framebufferDesc;
  for (const auto& output : m_Impl->OutputTextures) {
    framebufferDesc.addColorAttachment(output);
  }
  framebufferDesc.setDepthAttachment(m_Impl->DepthTexture);
  m_Impl->Framebuffer = nvrhiDevice->createFramebuffer(framebufferDesc);
  if (m_Impl->Framebuffer == nullptr) {
    return;
  }
}

ViewportTarget::~ViewportTarget() = default;
ViewportTarget::ViewportTarget(ViewportTarget&&) noexcept = default;
auto ViewportTarget::operator=(ViewportTarget&&) noexcept -> ViewportTarget& = default;

auto ViewportTarget::IsValid() const noexcept -> bool {
  return m_Impl && (m_Impl->ColorTexture != nullptr) && (m_Impl->DepthTexture != nullptr) &&
         (m_Impl->Framebuffer != nullptr);
}

auto ViewportTarget::GetWidth() const noexcept -> uint32_t { return m_Impl ? m_Impl->Width : 0; }

auto ViewportTarget::GetHeight() const noexcept -> uint32_t { return m_Impl ? m_Impl->Height : 0; }

auto ViewportTarget::GetColorTextureHandle() const noexcept -> void* {
  return m_Impl ? static_cast<void*>(m_Impl->ColorTexture.Get()) : nullptr;
}

auto ViewportTarget::GetFramebufferHandle() const noexcept -> void* {
  return m_Impl ? static_cast<void*>(m_Impl->Framebuffer.Get()) : nullptr;
}

auto ViewportTarget::GetOutputTextureHandle(uint32_t index) const noexcept -> void* {
  return m_Impl && index < m_Impl->OutputTextures.size() ? static_cast<void*>(m_Impl->OutputTextures.at(index).Get())
                                                         : nullptr;
}
} // namespace Engine

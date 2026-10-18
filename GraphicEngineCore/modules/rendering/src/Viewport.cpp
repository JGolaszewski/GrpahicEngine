#include "rendering/include/renderer/Viewport.h"

#include "rendering/include/ViewportTarget.h"

#include <utility>

namespace Engine {
Engine::Viewport::Viewport(uint32_t width, uint32_t height, void* framebufferHandle)
    : m_Width(width), m_Height(height), m_Framebuffer(framebufferHandle) {}

Viewport::Viewport(std::shared_ptr<ViewportTarget> target)
    : m_Width(target ? target->GetWidth() : 0), m_Height(target ? target->GetHeight() : 0),
      m_Framebuffer(target ? target->GetFramebufferHandle() : nullptr), m_Target(std::move(target)) {}

float Viewport::GetAspectRatio() const {
  if (m_Width == 0 || m_Height == 0) {
    return 1.0f;
  }

  return static_cast<float>(m_Width) / static_cast<float>(m_Height);
}

auto Viewport::GetSceneFramebufferHandle() const noexcept -> void* {
  return m_Target ? m_Target->GetSceneFramebufferHandle() : m_Framebuffer;
}

auto Viewport::GetDisplayColorTextureHandle() const noexcept -> void* {
  return m_Target ? m_Target->GetColorTextureHandle() : nullptr;
}

} // namespace Engine

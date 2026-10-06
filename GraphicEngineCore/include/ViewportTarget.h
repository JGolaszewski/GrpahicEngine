#pragma once

#include "CoreExport.h"

#include <cstdint>
#include <memory>

namespace Engine
{

class RenderSystem;
class Viewport;

// High-level render target for an editor or runtime viewport.
// The graphics API resources are deliberately hidden from callers.
class ENGINE_API ViewportTarget
{
  public:
    ~ViewportTarget();

    ViewportTarget(const ViewportTarget &) = delete;
    ViewportTarget &operator=(const ViewportTarget &) = delete;
    ViewportTarget(ViewportTarget &&) noexcept;
    ViewportTarget &operator=(ViewportTarget &&) noexcept;

    bool IsValid() const noexcept;
    uint32_t GetWidth() const noexcept;
    uint32_t GetHeight() const noexcept;

    void *GetColorTextureHandle() const noexcept;

  private:
    struct Impl;

    ViewportTarget(void *device, uint32_t width, uint32_t height);
    void *GetFramebufferHandle() const noexcept;

    std::unique_ptr<Impl> m_Impl;

    friend class RenderSystem;
    friend class Viewport;
};
} // namespace Engine

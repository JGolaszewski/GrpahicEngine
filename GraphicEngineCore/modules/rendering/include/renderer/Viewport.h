#pragma once

#include "runtime/include/CoreExport.h"

#include <memory>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Renderer
{
class Camera;
}

namespace Engine
{
class ViewportTarget;

/// Dimensions and opaque render-target handle associated with a scene view.
class ENGINE_API Viewport
{
  public:
    /// Creates a viewport over an externally owned framebuffer.
    /// @param width Framebuffer width in pixels.
    /// @param height Framebuffer height in pixels.
    /// @param framebufferHandle Opaque backend framebuffer handle.
    Viewport(uint32_t width, uint32_t height, void *framebufferHandle);

    /// Creates a viewport backed by an engine-owned render target.
    /// @param target Render target whose framebuffer is used by this viewport.
    explicit Viewport(std::shared_ptr<ViewportTarget> target);

    /// Returns the framebuffer width in pixels.
    /// @return Viewport width.
    inline uint32_t GetWidth() const
    {
        return m_Width;
    }
    /// Returns the framebuffer height in pixels.
    /// @return Viewport height.
    inline uint32_t GetHeight() const
    {
        return m_Height;
    }
    /// Returns width divided by height, or zero for a zero-height viewport.
    /// @return Viewport aspect ratio.
    float GetAspectRatio() const;

    /// Returns the opaque framebuffer handle used by backend integration.
    /// @return Framebuffer handle, or null when no target is attached.
    void *GetFramebufferHandle() const noexcept
    {
        return m_Framebuffer;
    }

  private:
    uint32_t m_Width;
    uint32_t m_Height;
    void *m_Framebuffer;
    std::shared_ptr<ViewportTarget> m_Target;
};

} // namespace Engine

#ifdef _MSC_VER
#pragma warning(pop)
#endif

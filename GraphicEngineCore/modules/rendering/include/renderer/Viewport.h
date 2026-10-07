#pragma once

#include "runtime/include/CoreExport.h"

#include "scene/include/Scene.h"
#include <memory>
#include <nvrhi/nvrhi.h>

namespace Engine::Renderer
{
class Camera;
}

namespace Engine
{
class ViewportTarget;

class ENGINE_API Viewport
{
  public:
    Viewport(uint32_t width, uint32_t height, nvrhi::IFramebuffer *framebuffer);
    explicit Viewport(std::shared_ptr<ViewportTarget> target);

    inline uint32_t GetWidth() const
    {
        return m_Width;
    }
    inline uint32_t GetHeight() const
    {
        return m_Height;
    }
    float GetAspectRatio() const;
    nvrhi::IFramebuffer *GetFramebuffer() const
    {
        return m_Framebuffer;
    }

  private:
    uint32_t m_Width;
    uint32_t m_Height;
    nvrhi::IFramebuffer *m_Framebuffer;
    std::shared_ptr<ViewportTarget> m_Target;
};

} // namespace Engine

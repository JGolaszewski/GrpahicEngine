#pragma once

#include "runtime/include/CoreExport.h"
#include "rendering/src/render_graph/RGBuilder.h"
#include "rendering/src/render_graph/RenderContext.h"
#include "rendering/include/renderer/Light.h"
#include "rendering/src/RenderPacket.h"
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine
{
class SceneRenderer;
}

namespace Engine::Renderer
{

class FrameBlackboard;
class RenderContext;

class ENGINE_API IRenderPass
{
  public:
    virtual ~IRenderPass() = default;

    virtual std::string GetName() const = 0;
    virtual bool HasSideEffect() const
    {
        return false;
    }
    virtual void Setup(RGBuilder &builder, FrameBlackboard &blackboard) = 0;
    virtual bool Execute(nvrhi::ICommandList *cmd, const RenderContext &ctx) = 0;
    virtual RGResourceID GetOutputID() const
    {
        return RGResourceID();
    }
};

class ENGINE_API ShadowPass : public IRenderPass
{
  public:
    ShadowPass();

    std::string GetName() const override
    {
        return "ShadowPass";
    }

    void Setup(::Engine::Renderer::RGBuilder &builder,
               ::Engine::Renderer::FrameBlackboard &blackboard) override;
    bool Execute(nvrhi::ICommandList *cmd, const ::Engine::Renderer::RenderContext &ctx) override;
    RGResourceID GetOutputID() const override
    {
        return m_ShadowMapID;
    }

  private:
    RGResourceID m_ShadowMapID;
    nvrhi::TextureHandle m_ShadowMapTexture;
    std::vector<nvrhi::FramebufferHandle> m_ShadowFramebuffers;
};

class ENGINE_API ForwardPass : public ::Engine::Renderer::IRenderPass
{
  private:
    ::Engine::Renderer::RGResourceID m_ShadowMapInputID;
    Engine::SceneRenderer *m_SceneRenderer = nullptr;

  public:
    explicit ForwardPass(Engine::SceneRenderer *sceneRenderer);

    std::string GetName() const override
    {
        return "ForwardPass";
    }
    bool HasSideEffect() const override
    {
        return true;
    }

    void Setup(::Engine::Renderer::RGBuilder &builder,
               ::Engine::Renderer::FrameBlackboard &blackboard) override;
    bool Execute(nvrhi::ICommandList *cmd, const ::Engine::Renderer::RenderContext &ctx) override;
};

class ENGINE_API DebugPass : public ::Engine::Renderer::IRenderPass
{
  public:
    explicit DebugPass(Engine::SceneRenderer *sceneRenderer);

    std::string GetName() const override
    {
        return "DebugPass";
    }
    bool HasSideEffect() const override
    {
        return true;
    }

    void Setup(::Engine::Renderer::RGBuilder &builder,
               ::Engine::Renderer::FrameBlackboard &blackboard) override;
    bool Execute(nvrhi::ICommandList *cmd, const ::Engine::Renderer::RenderContext &ctx) override;

  private:
    Engine::SceneRenderer *m_SceneRenderer = nullptr;
};
} // namespace Engine::Renderer

#ifdef _MSC_VER
#pragma warning(pop)
#endif

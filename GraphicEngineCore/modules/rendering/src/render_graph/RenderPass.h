#pragma once

#include "rendering/include/renderer/Light.h"
#include "rendering/src/RenderPacket.h"
#include "rendering/src/render_graph/RGBuilder.h"
#include "rendering/src/render_graph/RenderContext.h"
#include "scene/include/components/CameraPostProcessing.h"
#include "runtime/include/CoreExport.h"
#include <memory>
#include <string>
#include <vector>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine {
class SceneRenderer;
}

namespace Engine::Renderer {

class FrameBlackboard;
class RenderContext;
class RenderView;

class ENGINE_API IRenderPass {
public:
  virtual ~IRenderPass() = default;

  virtual std::string GetName() const = 0;
  virtual bool HasSideEffect() const { return false; }
  virtual void Setup(RGBuilder& builder, FrameBlackboard& blackboard) = 0;
  virtual bool Execute(nvrhi::ICommandList* cmd, const RenderContext& ctx) = 0;
  virtual RGResourceID GetOutputID() const { return RGResourceID(); }
};

class ENGINE_API ShadowPass : public IRenderPass {
public:
  ShadowPass();

  std::string GetName() const override { return "ShadowPass"; }

  void Setup(::Engine::Renderer::RGBuilder& builder, ::Engine::Renderer::FrameBlackboard& blackboard) override;
  bool Execute(nvrhi::ICommandList* cmd, const ::Engine::Renderer::RenderContext& ctx) override;
  RGResourceID GetOutputID() const override { return m_ShadowMapID; }

private:
  RGResourceID m_ShadowMapID;
  nvrhi::TextureHandle m_ShadowMapTexture;
  std::vector<nvrhi::FramebufferHandle> m_ShadowFramebuffers;
};

class ENGINE_API ForwardPass : public ::Engine::Renderer::IRenderPass {
private:
  ::Engine::Renderer::RGResourceID m_ShadowMapInputID;
  Engine::SceneRenderer* m_SceneRenderer = nullptr;

public:
  explicit ForwardPass(Engine::SceneRenderer* sceneRenderer);

  std::string GetName() const override { return "ForwardPass"; }
  bool HasSideEffect() const override { return true; }

  void Setup(::Engine::Renderer::RGBuilder& builder, ::Engine::Renderer::FrameBlackboard& blackboard) override;
  bool Execute(nvrhi::ICommandList* cmd, const ::Engine::Renderer::RenderContext& ctx) override;
};

class ENGINE_API DebugPass : public ::Engine::Renderer::IRenderPass {
public:
  explicit DebugPass(Engine::SceneRenderer* sceneRenderer);

  std::string GetName() const override { return "DebugPass"; }
  bool HasSideEffect() const override { return true; }

  void Setup(::Engine::Renderer::RGBuilder& builder, ::Engine::Renderer::FrameBlackboard& blackboard) override;
  bool Execute(nvrhi::ICommandList* cmd, const ::Engine::Renderer::RenderContext& ctx) override;

private:
  Engine::SceneRenderer* m_SceneRenderer = nullptr;
};

struct CameraPostProcessResources {
  nvrhi::DeviceHandle Device;
  nvrhi::ShaderHandle VertexShader;
  nvrhi::ShaderHandle PixelShader;
  nvrhi::BindingLayoutHandle BindingLayout;
  nvrhi::BufferHandle ConstantsBuffer;
  nvrhi::GraphicsPipelineHandle Pipeline;
};

class ENGINE_API CameraPostProcessPass : public ::Engine::Renderer::IRenderPass {
public:
  CameraPostProcessPass(std::shared_ptr<CameraPostProcessResources> resources, size_t effectIndex);

  std::string GetName() const override { return "CameraPostProcessPass"; }
  bool HasSideEffect() const override { return true; }
  void Setup(RGBuilder& builder, FrameBlackboard& blackboard) override;
  bool Execute(nvrhi::ICommandList* cmd, const RenderContext& ctx) override;

private:
  struct ViewResources {
    RenderView* View = nullptr;
    RGResourceID Input;
    RGResourceID Output;
    Engine::Ecs::CameraPostProcessEffect Effect;
  };

  std::shared_ptr<CameraPostProcessResources> m_Resources;
  size_t m_EffectIndex = 0;
  std::vector<ViewResources> m_Views;
};

class ENGINE_API CameraPostProcessCompositePass : public ::Engine::Renderer::IRenderPass {
public:
  explicit CameraPostProcessCompositePass(std::shared_ptr<CameraPostProcessResources> resources);
  std::string GetName() const override { return "CameraPostProcessCompositePass"; }
  bool HasSideEffect() const override { return true; }
  void Setup(RGBuilder& builder, FrameBlackboard& blackboard) override;
  bool Execute(nvrhi::ICommandList* cmd, const RenderContext& ctx) override;

private:
  struct ViewResources {
    RenderView* View = nullptr;
    RGResourceID Input;
    RGResourceID Output;
  };
  std::shared_ptr<CameraPostProcessResources> m_Resources;
  std::vector<ViewResources> m_Views;
};

struct DisplayTransformResources {
  nvrhi::DeviceHandle Device;
  nvrhi::ShaderHandle VertexShader;
  nvrhi::ShaderHandle PixelShader;
  nvrhi::BindingLayoutHandle BindingLayout;
  nvrhi::GraphicsPipelineHandle Pipeline;
};

class ENGINE_API DisplayTransformPass : public ::Engine::Renderer::IRenderPass {
public:
  explicit DisplayTransformPass(std::shared_ptr<DisplayTransformResources> resources);

  std::string GetName() const override { return "DisplayTransformPass"; }
  bool HasSideEffect() const override { return true; }

  void Setup(::Engine::Renderer::RGBuilder& builder, ::Engine::Renderer::FrameBlackboard& blackboard) override;
  bool Execute(nvrhi::ICommandList* cmd, const ::Engine::Renderer::RenderContext& ctx) override;

private:
  struct ViewResources {
    RenderView* View = nullptr;
    RGResourceID SceneColor;
    RGResourceID DisplayColor;
  };

  std::shared_ptr<DisplayTransformResources> m_Resources;
  std::vector<ViewResources> m_Views;
};
} // namespace Engine::Renderer

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

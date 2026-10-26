#include "rendering/src/render_graph/RenderPass.h"
#include "rendering/src/SceneRenderer.h"
#include "rendering/src/render_graph/FrameBlackboard.h"
#include "rendering/src/render_graph/RenderView.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr uint32_t ShadowMapSize = 2048;
constexpr uint32_t MaxShadowedLights = 4;

auto DrawShadowQueue(nvrhi::ICommandList* commandList, const std::vector<::Engine::Renderer::RenderPacket>& packets,
                     const nvrhi::BufferHandle& perObjectBuffer, const nvrhi::BindingSetHandle& perObjectBindings,
                     nvrhi::IFramebuffer* framebuffer, const nvrhi::Viewport& viewport, const nvrhi::Rect& scissor,
                     const ::Engine::Renderer::RenderContext& context) -> bool {
  for (const auto& packet : packets) {
    if ((packet.Pipeline == nullptr) || (packet.VertexBuffer == nullptr) ||
        (packet.ShadowAlphaTest && (packet.MaterialBindings == nullptr))) {
      return false;
    }
    commandList->writeBuffer(perObjectBuffer, &packet.PerObjectData, sizeof(packet.PerObjectData));
    context.GetFrameContext().GetDiagnostics().UploadBytes += sizeof(packet.PerObjectData);
    ++context.GetFrameContext().GetDiagnostics().DrawCalls;
    nvrhi::GraphicsState state;
    state.pipeline = packet.Pipeline;
    state.framebuffer = framebuffer;
    state.viewport.addViewport(viewport);
    state.viewport.addScissorRect(scissor);
    state.bindings = {perObjectBindings};
    if (packet.ShadowAlphaTest) {
      state.bindings.push_back(packet.MaterialBindings);
    }
    nvrhi::VertexBufferBinding vertexBuffer;
    vertexBuffer.buffer = packet.VertexBuffer;
    vertexBuffer.slot = 0;
    vertexBuffer.offset = 0;
    state.vertexBuffers = {vertexBuffer};
    if (packet.IndexBuffer != nullptr) {
      state.indexBuffer.buffer = packet.IndexBuffer;
      state.indexBuffer.format = nvrhi::Format::R32_UINT;
      state.indexBuffer.offset = 0;
    }
    commandList->setGraphicsState(state);
    nvrhi::DrawArguments arguments;
    arguments.instanceCount = 1;
    arguments.startVertexLocation = packet.BaseVertexOffset;
    if (packet.IndexBuffer != nullptr) {
      arguments.startIndexLocation = packet.StartIndexLocation;
      arguments.vertexCount = packet.IndexCount;
      commandList->drawIndexed(arguments);
    } else {
      arguments.vertexCount = packet.VertexCount;
      commandList->draw(arguments);
    }
  }
  return true;
}

auto DrawForwardQueue(nvrhi::ICommandList* commandList, const std::vector<::Engine::Renderer::RenderPacket>& packets,
                      const nvrhi::BufferHandle& perObjectBuffer, const nvrhi::BindingSetHandle& perObjectBindings,
                      const nvrhi::BindingSetHandle& sceneBindings, nvrhi::IFramebuffer* framebuffer,
                      const nvrhi::Viewport& viewport, const nvrhi::Rect& scissor,
                      const ::Engine::Renderer::RenderContext& context) -> bool {
  for (const auto& packet : packets) {
    if ((packet.Pipeline == nullptr) || (packet.VertexBuffer == nullptr) || (sceneBindings == nullptr)) {
      return false;
    }
    commandList->writeBuffer(perObjectBuffer, &packet.PerObjectData, sizeof(packet.PerObjectData));
    context.GetFrameContext().GetDiagnostics().UploadBytes += sizeof(packet.PerObjectData);
    nvrhi::GraphicsState state;
    state.pipeline = packet.Pipeline;
    state.framebuffer = framebuffer;
    state.viewport.addViewport(viewport);
    state.viewport.addScissorRect(scissor);
    state.bindings = {perObjectBindings, sceneBindings, packet.MaterialBindings};
    nvrhi::VertexBufferBinding vertexBuffer;
    vertexBuffer.buffer = packet.VertexBuffer;
    vertexBuffer.slot = 0;
    vertexBuffer.offset = 0;
    state.vertexBuffers = {vertexBuffer};
    if (packet.IndexBuffer != nullptr) {
      state.indexBuffer.buffer = packet.IndexBuffer;
      state.indexBuffer.format = nvrhi::Format::R32_UINT;
      state.indexBuffer.offset = 0;
    }
    commandList->setGraphicsState(state);
    nvrhi::DrawArguments arguments;
    arguments.instanceCount = 1;
    arguments.startVertexLocation = packet.BaseVertexOffset;
    if (packet.IndexBuffer != nullptr) {
      arguments.startIndexLocation = packet.StartIndexLocation;
      arguments.vertexCount = packet.IndexCount;
      commandList->drawIndexed(arguments);
    } else {
      arguments.vertexCount = packet.VertexCount;
      commandList->draw(arguments);
    }
    ++context.GetFrameContext().GetDiagnostics().DrawCalls;
  }
  return true;
}

auto ClearForwardAttachments(nvrhi::ICommandList* commandList,
                             const std::vector<::Engine::Renderer::RGViewportAttachment>& attachments,
                             const ::Engine::Renderer::RenderContext& context) -> bool {
  return std::ranges::all_of(attachments, [&](const ::Engine::Renderer::RGViewportAttachment& output) -> bool {
    if (output.Load != ::Engine::Renderer::RGAttachmentLoadOp::Clear) {
      return true;
    }
    nvrhi::TextureHandle texture = context.GetPhysicalTexture(output.Resource);
    if (texture == nullptr) {
      return false;
    }
    if (output.IsDepth) {
      commandList->clearDepthStencilTexture(texture, output.Subresources, true, 1.0F, false, 0);
    } else {
      commandList->clearTextureFloat(texture, output.Subresources, output.ClearColor);
    }
    return true;
  });
}

auto DescribeColorTexture(uint32_t width, uint32_t height) -> ::Engine::Renderer::RGTextureDesc;
} // namespace

namespace Engine::Renderer {

// ==============================================================================
// SHADOW PASS
// ==============================================================================

ShadowPass::ShadowPass() = default;

void ShadowPass::Setup(::Engine::Renderer::RGBuilder& builder, ::Engine::Renderer::FrameBlackboard& blackboard) {
  uint32_t shadowMapSize = blackboard.Get<uint32_t>("ShadowMapSize", ShadowMapSize);
  uint32_t maxShadowedLights = blackboard.Get<uint32_t>("MaxShadowedLights", MaxShadowedLights);

  ::Engine::Renderer::RGTextureDesc desc{};
  desc.width = shadowMapSize;
  desc.height = shadowMapSize;
  desc.format = nvrhi::Format::D32;
  desc.arraySize = maxShadowedLights;
  desc.dimension = nvrhi::TextureDimension::Texture2DArray;
  desc.isRenderTarget = true;
  desc.isShaderResource = true;
  desc.mipLevels = 1;

  m_ShadowMapID = builder.CreateTexture(desc);
  const auto& shadowViews = blackboard.GetShadowViews();
  if (shadowViews.empty()) {
    builder.Write(m_ShadowMapID, nvrhi::ResourceStates::DepthWrite);
  }
  for (uint32_t shadowSlot = 0; shadowSlot < shadowViews.size(); ++shadowSlot) {
    nvrhi::TextureSubresourceSet subresources;
    subresources.baseArraySlice = shadowSlot;
    subresources.numArraySlices = 1;
    subresources.baseMipLevel = 0;
    subresources.numMipLevels = 1;
    builder.Write(m_ShadowMapID, nvrhi::ResourceStates::DepthWrite, subresources);
  }
  blackboard.SetShadowMap(m_ShadowMapID);
}

auto ShadowPass::Execute(nvrhi::ICommandList* cmd, const ::Engine::Renderer::RenderContext& ctx) -> bool {
  const auto& shadowViews = ctx.GetBlackboard().GetShadowViews();
  if (shadowViews.empty()) {
    return true;
  }
  nvrhi::BufferHandle perObjectBuffer = ctx.GetBlackboard().GetPerObjectBuffer();
  nvrhi::BindingSetHandle perObjectBindings = ctx.GetBlackboard().GetPerObjectBindingSet();
  if ((perObjectBuffer == nullptr) || (perObjectBindings == nullptr)) {
    return false;
  }

  nvrhi::TextureHandle physicalShadowMap = ctx.GetPhysicalTexture(m_ShadowMapID);
  if (physicalShadowMap == nullptr) {
    return false;
  }

  const nvrhi::TextureDesc& shadowTexDesc = physicalShadowMap->getDesc();

  nvrhi::Viewport shadowViewport =
      nvrhi::Viewport(static_cast<float>(shadowTexDesc.width), static_cast<float>(shadowTexDesc.height));
  nvrhi::Rect shadowScissor =
      nvrhi::Rect(static_cast<int>(shadowTexDesc.width), static_cast<int>(shadowTexDesc.height));

  if ((m_ShadowMapTexture == nullptr) || m_ShadowMapTexture.Get() != physicalShadowMap.Get()) {
    m_ShadowMapTexture = physicalShadowMap;
    m_ShadowFramebuffers.clear();
  }

  for (uint32_t shadowSlot = 0; shadowSlot < shadowViews.size(); ++shadowSlot) {
    ::Engine::Renderer::RenderView* view = shadowViews.at(shadowSlot);
    if (view == nullptr) {
      continue;
    }

    if (shadowSlot >= m_ShadowFramebuffers.size()) {
      m_ShadowFramebuffers.resize(shadowSlot + 1);
    }

    if (m_ShadowFramebuffers.at(shadowSlot) == nullptr) {
      nvrhi::FramebufferDesc desc;
      desc.depthAttachment.texture = physicalShadowMap;
      desc.depthAttachment.subresources.baseArraySlice = shadowSlot;
      desc.depthAttachment.subresources.numArraySlices = 1;
      desc.depthAttachment.subresources.baseMipLevel = 0;
      desc.depthAttachment.subresources.numMipLevels = 1;
      m_ShadowFramebuffers.at(shadowSlot) = ctx.GetDevice()->createFramebuffer(desc);
    }
    nvrhi::IFramebuffer* shadowFb = m_ShadowFramebuffers.at(shadowSlot);
    if (shadowFb == nullptr) {
      return false;
    }

    nvrhi::TextureSubresourceSet subresources;
    subresources.baseArraySlice = shadowSlot;
    subresources.numArraySlices = 1;
    subresources.baseMipLevel = 0;
    subresources.numMipLevels = 1;
    cmd->clearDepthStencilTexture(physicalShadowMap, subresources, true, 1.0F, false, 0);

    if (!DrawShadowQueue(cmd, view->Queues.Opaque, perObjectBuffer, perObjectBindings, shadowFb, shadowViewport,
                         shadowScissor, ctx)) {
      return false;
    }
  }
  return true;
}

// ==============================================================================
// FORWARD PASS
// ==============================================================================

ForwardPass::ForwardPass(Engine::SceneRenderer* sceneRenderer) : m_SceneRenderer(sceneRenderer) {}

void ForwardPass::Setup(::Engine::Renderer::RGBuilder& builder, FrameBlackboard& blackboard) {
  m_ShadowMapInputID = blackboard.GetShadowMap();
  builder.Read(m_ShadowMapInputID);
  for (::Engine::Renderer::RenderView* view : blackboard.GetSceneViews()) {
    if ((view == nullptr) || (view->TargetFramebuffer == nullptr)) {
      continue;
    }
    if (Engine::Ecs::HasScreenSpaceAmbientOcclusion(view->PostProcessing) && view->AmbientOcclusionTexture.isValid()) {
      builder.Read(view->AmbientOcclusionTexture, nvrhi::ResourceStates::ShaderResource);
    }
    view->OutputAttachments.clear();
    const nvrhi::FramebufferDesc& framebuffer = view->TargetFramebuffer->getDesc();
    auto importAttachment = [&](const nvrhi::FramebufferAttachment& attachment, bool depth) -> void {
      if (!attachment.texture) {
        return;
      }
      const nvrhi::TextureDesc& texture = attachment.texture->getDesc();
      ::Engine::Renderer::RGTextureDesc desc;
      desc.width = texture.width;
      desc.height = texture.height;
      desc.arraySize = texture.arraySize;
      desc.mipLevels = texture.mipLevels;
      desc.isRenderTarget = texture.isRenderTarget;
      desc.isShaderResource = texture.isShaderResource;
      desc.isUnorderedAccess = texture.isUAV;
      desc.format = texture.format;
      desc.dimension = texture.dimension;
      const nvrhi::ResourceStates writeState =
          depth ? nvrhi::ResourceStates::DepthWrite : nvrhi::ResourceStates::RenderTarget;
      const nvrhi::ResourceStates initialState =
          texture.initialState == nvrhi::ResourceStates::Unknown ? writeState : texture.initialState;
      const nvrhi::ResourceStates finalState =
          depth || !texture.isShaderResource ? writeState : nvrhi::ResourceStates::ShaderResource;
      const ::Engine::Renderer::RGResourceID id =
          builder.ImportTexture(desc, nvrhi::TextureHandle(attachment.texture), initialState, finalState);
      builder.Write(id, writeState, attachment.subresources);
      ::Engine::Renderer::RGViewportAttachment output;
      output.Resource = id;
      output.Subresources = attachment.subresources;
      output.IsDepth = depth;
      view->OutputAttachments.push_back(output);
    };
    uint32_t colorIndex = 0;
    for (const nvrhi::FramebufferAttachment& attachment : framebuffer.colorAttachments) {
      importAttachment(attachment, false);
      if (view->RenderMode == Engine::ViewportBinding::Mode::Final && !view->OutputAttachments.empty()) {
        auto& output = view->OutputAttachments.back();
        output.ClearColor =
            colorIndex == 1 ? nvrhi::Color(1.0F, 1.0F, 1.0F, 1.0F) : nvrhi::Color(0.0F, 0.0F, 0.0F, 0.0F);
      } else if (view->RenderMode != Engine::ViewportBinding::Mode::Final && !view->OutputAttachments.empty()) {
        view->OutputAttachments.back().ClearColor = nvrhi::Color(0.02F, 0.02F, 0.02F, 1.0F);
      }
      ++colorIndex;
    }
    if (framebuffer.depthAttachment.valid()) {
      importAttachment(framebuffer.depthAttachment, true);
    }
  }
}

auto ForwardPass::Execute(nvrhi::ICommandList* cmd, const ::Engine::Renderer::RenderContext& ctx) -> bool {
  const auto& sceneViews = ctx.GetBlackboard().GetSceneViews();
  if (sceneViews.empty()) {
    return true;
  }

  nvrhi::TextureHandle physicalShadowMap = ctx.GetPhysicalTexture(m_ShadowMapInputID);
  nvrhi::BufferHandle perObjectBuffer = ctx.GetBlackboard().GetPerObjectBuffer();
  nvrhi::BindingSetHandle perObjectBindings = ctx.GetBlackboard().GetPerObjectBindingSet();
  nvrhi::SamplerHandle shadowSampler = ctx.GetBlackboard().GetShadowSampler();
  nvrhi::BindingLayoutHandle sceneBindingLayout = ctx.GetBlackboard().GetSceneBindingLayout();
  if ((physicalShadowMap == nullptr) || (perObjectBuffer == nullptr) || (perObjectBindings == nullptr) ||
      (shadowSampler == nullptr) || (sceneBindingLayout == nullptr)) {
    return false;
  }

  for (size_t viewIndex = 0; viewIndex < sceneViews.size(); ++viewIndex) {
    ::Engine::Renderer::RenderView* view = sceneViews.at(viewIndex);
    if ((view == nullptr) || (view->TargetFramebuffer == nullptr) || (view->CameraBuffer == nullptr) ||
        (view->LightBuffer == nullptr) ||
        (Engine::Ecs::HasScreenSpaceAmbientOcclusion(view->PostProcessing) &&
         !view->AmbientOcclusionTexture.isValid())) {
      return false;
    }
    ::Engine::Renderer::LightsCBData lightData{};
    const uint32_t lightCount =
        static_cast<uint32_t>(std::min(view->Lights.size(), static_cast<size_t>(ENGINE_MAX_LIGHTS)));
    for (uint32_t i = 0; i < lightCount; ++i) {
      lightData.Lights[i] = view->Lights.at(i);
    }
    lightData.LightCount = lightCount;
    lightData.EnvironmentEnabled = view->Environment.Preset == Engine::EnvironmentPreset::Studio ? 1U : 0U;
    lightData.EnvironmentIntensity = view->Environment.Intensity;
    cmd->writeBuffer(view->LightBuffer, &lightData, sizeof(lightData));
    ctx.GetFrameContext().GetDiagnostics().UploadBytes += sizeof(lightData);
    nvrhi::TextureHandle ambientOcclusion;
    if (Engine::Ecs::HasScreenSpaceAmbientOcclusion(view->PostProcessing)) {
      ambientOcclusion = ctx.GetPhysicalTexture(view->AmbientOcclusionTexture);
    }
    nvrhi::BindingSetHandle sceneBindings =
        (m_SceneRenderer != nullptr)
            ? m_SceneRenderer->GetSceneBindingSet(viewIndex, view->LightBuffer, physicalShadowMap, shadowSampler,
                                                  view->CameraBuffer, ambientOcclusion,
                                                  ctx.GetFrameContext().GetDiagnostics())
            : nullptr;
    if (sceneBindings == nullptr) {
      return false;
    }

    nvrhi::IFramebuffer* mainFramebuffer = view->TargetFramebuffer;
    if (!ClearForwardAttachments(cmd, view->OutputAttachments, ctx)) {
      return false;
    }

    const nvrhi::FramebufferInfoEx& fbInfo = mainFramebuffer->getFramebufferInfo();

    nvrhi::Viewport viewportState =
        nvrhi::Viewport(static_cast<float>(fbInfo.width), static_cast<float>(fbInfo.height));
    nvrhi::Rect scissor = nvrhi::Rect(static_cast<int>(fbInfo.width), static_cast<int>(fbInfo.height));

    if (!DrawForwardQueue(cmd, view->Queues.Opaque, perObjectBuffer, perObjectBindings, sceneBindings, mainFramebuffer,
                          viewportState, scissor, ctx) ||
        !DrawForwardQueue(cmd, view->Queues.Masked, perObjectBuffer, perObjectBindings, sceneBindings, mainFramebuffer,
                          viewportState, scissor, ctx) ||
        !DrawForwardQueue(cmd, view->Queues.Skybox, perObjectBuffer, perObjectBindings, sceneBindings, mainFramebuffer,
                          viewportState, scissor, ctx) ||
        !DrawForwardQueue(cmd, view->Queues.Transparent, perObjectBuffer, perObjectBindings, sceneBindings,
                          mainFramebuffer, viewportState, scissor, ctx) ||
        !DrawForwardQueue(cmd, view->Queues.UI, perObjectBuffer, perObjectBindings, sceneBindings, mainFramebuffer,
                          viewportState, scissor, ctx)) {
      return false;
    }
  }
  return true;
}

// ==============================================================================
// DEBUG PASS
// ==============================================================================

DebugPass::DebugPass(Engine::SceneRenderer* sceneRenderer) : m_SceneRenderer(sceneRenderer) {}

void DebugPass::Setup(::Engine::Renderer::RGBuilder& builder, FrameBlackboard& blackboard) {
  for (::Engine::Renderer::RenderView* view : blackboard.GetSceneViews()) {
    if (view == nullptr || view->RenderMode != Engine::ViewportBinding::Mode::Editor) {
      continue;
    }
    for (const ::Engine::Renderer::RGViewportAttachment& output : view->OutputAttachments) {
      builder.Write(output.Resource,
                    output.IsDepth ? nvrhi::ResourceStates::DepthWrite : nvrhi::ResourceStates::RenderTarget,
                    output.Subresources);
    }
  }
}

auto DebugPass::Execute(nvrhi::ICommandList* cmd, const ::Engine::Renderer::RenderContext& ctx) -> bool {
  if (m_SceneRenderer == nullptr) {
    return false;
  }

  for (const ::Engine::Renderer::RenderView* view : ctx.GetBlackboard().GetSceneViews()) {
    if ((view != nullptr) && view->RenderMode == Engine::ViewportBinding::Mode::Editor) {
      m_SceneRenderer->RenderDebugGeometry(cmd, *view);
    }
  }
  return true;
}

CameraAmbientOcclusionGeometryPass::CameraAmbientOcclusionGeometryPass(
    std::shared_ptr<AmbientOcclusionResources> resources, Engine::SceneRenderer* sceneRenderer)
    : m_Resources(std::move(resources)), m_SceneRenderer(sceneRenderer) {}

void CameraAmbientOcclusionGeometryPass::Setup(RGBuilder& builder, FrameBlackboard& blackboard) {
  m_Views.clear();
  for (RenderView* view : blackboard.GetSceneViews()) {
    if (view == nullptr || !Engine::Ecs::HasScreenSpaceAmbientOcclusion(view->PostProcessing) ||
        view->TargetFramebuffer == nullptr) {
      continue;
    }
    const RGTextureDesc colorDesc = DescribeColorTexture(view->ViewportWidth, view->ViewportHeight);
    RGTextureDesc depthDesc = colorDesc;
    depthDesc.format = nvrhi::Format::R32_FLOAT;
    RGTextureDesc depthStencilDesc = colorDesc;
    depthStencilDesc.isShaderResource = false;
    depthStencilDesc.format = nvrhi::Format::D24S8;
    view->AmbientOcclusionDepth = builder.CreateTexture(depthDesc);
    builder.Write(view->AmbientOcclusionDepth, nvrhi::ResourceStates::RenderTarget);
    RGTextureDesc normalDesc = colorDesc;
    normalDesc.format = nvrhi::Format::RGBA16_FLOAT;
    view->AmbientOcclusionNormal = builder.CreateTexture(normalDesc);
    builder.Write(view->AmbientOcclusionNormal, nvrhi::ResourceStates::RenderTarget);
    const RGResourceID depthStencil = builder.CreateTexture(depthStencilDesc);
    builder.Write(depthStencil, nvrhi::ResourceStates::DepthWrite);
    m_Views.push_back({.View = view,
                       .Depth = view->AmbientOcclusionDepth,
                       .Normal = view->AmbientOcclusionNormal,
                       .DepthStencil = depthStencil});
  }
}

auto CameraAmbientOcclusionGeometryPass::Execute(nvrhi::ICommandList* cmd, const RenderContext& ctx) -> bool {
  if (m_Views.empty()) {
    return true;
  }
  if (cmd == nullptr || m_Resources == nullptr || m_Resources->Device == nullptr || m_SceneRenderer == nullptr ||
      m_SceneRenderer->GetPipelineCache() == nullptr) {
    return false;
  }
  const nvrhi::BufferHandle perObjectBuffer = ctx.GetBlackboard().GetPerObjectBuffer();
  const nvrhi::BindingSetHandle perObjectBindings = ctx.GetBlackboard().GetPerObjectBindingSet();
  if (perObjectBuffer == nullptr || perObjectBindings == nullptr) {
    return false;
  }

  for (const ViewResources& viewResources : m_Views) {
    RenderView* view = viewResources.View;
    nvrhi::TextureHandle depth = ctx.GetPhysicalTexture(viewResources.Depth);
    nvrhi::TextureHandle normal = ctx.GetPhysicalTexture(viewResources.Normal);
    nvrhi::TextureHandle depthStencil = ctx.GetPhysicalTexture(viewResources.DepthStencil);
    if (view == nullptr || depth == nullptr || normal == nullptr || depthStencil == nullptr) {
      return false;
    }
    nvrhi::FramebufferDesc framebufferDesc;
    framebufferDesc.addColorAttachment(depth);
    framebufferDesc.addColorAttachment(normal);
    framebufferDesc.setDepthAttachment(depthStencil);
    nvrhi::FramebufferHandle framebuffer = m_Resources->Device->createFramebuffer(framebufferDesc);
    if (framebuffer == nullptr) {
      return false;
    }
    cmd->clearTextureFloat(depth, nvrhi::AllSubresources, nvrhi::Color(1.0F, 1.0F, 1.0F, 1.0F));
    cmd->clearTextureFloat(normal, nvrhi::AllSubresources, nvrhi::Color(0.0F, 0.0F, 0.0F, 0.0F));
    cmd->clearDepthStencilTexture(depthStencil, nvrhi::AllSubresources, true, 1.0F, true, 0);
    const nvrhi::FramebufferInfoEx& info = framebuffer->getFramebufferInfo();
    const nvrhi::Viewport viewport(static_cast<float>(info.width), static_cast<float>(info.height));
    const nvrhi::Rect scissor(static_cast<int>(info.width), static_cast<int>(info.height));

    const auto drawQueue = [&](const std::vector<RenderPacket>& packets) -> bool {
      for (const RenderPacket& packet : packets) {
        if (packet.VertexBuffer == nullptr || packet.MaterialBindings == nullptr ||
            !packet.AmbientOcclusionPipelineKey) {
          return false;
        }
        const nvrhi::GraphicsPipelineHandle pipeline = m_SceneRenderer->GetPipelineCache()->GetOrCreatePipeline(
            *packet.AmbientOcclusionPipelineKey, framebuffer->getFramebufferInfo());
        if (pipeline == nullptr) {
          return false;
        }
        cmd->writeBuffer(perObjectBuffer, &packet.PerObjectData, sizeof(packet.PerObjectData));
        ctx.GetFrameContext().GetDiagnostics().UploadBytes += sizeof(packet.PerObjectData);
        nvrhi::GraphicsState state;
        state.pipeline = pipeline;
        state.framebuffer = framebuffer;
        state.viewport.addViewport(viewport);
        state.viewport.addScissorRect(scissor);
        state.bindings = {perObjectBindings, packet.MaterialBindings};
        state.vertexBuffers = {nvrhi::VertexBufferBinding{.buffer = packet.VertexBuffer, .slot = 0, .offset = 0}};
        if (packet.IndexBuffer != nullptr) {
          state.indexBuffer.buffer = packet.IndexBuffer;
          state.indexBuffer.format = nvrhi::Format::R32_UINT;
          state.indexBuffer.offset = 0;
        }
        cmd->setGraphicsState(state);
        nvrhi::DrawArguments arguments;
        arguments.instanceCount = 1;
        arguments.startVertexLocation = packet.BaseVertexOffset;
        if (packet.IndexBuffer != nullptr) {
          arguments.startIndexLocation = packet.StartIndexLocation;
          arguments.vertexCount = packet.IndexCount;
          cmd->drawIndexed(arguments);
        } else {
          arguments.vertexCount = packet.VertexCount;
          cmd->draw(arguments);
        }
        ++ctx.GetFrameContext().GetDiagnostics().DrawCalls;
      }
      return true;
    };
    if (!drawQueue(view->Queues.Opaque) || !drawQueue(view->Queues.Masked)) {
      return false;
    }
  }
  return true;
}

namespace {
struct AmbientOcclusionConstants {
  float NearPlane = 0.1F;
  float FarPlane = 1000.0F;
  float Radius = 0.5F;
  float Intensity = 1.0F;
  float TanHalfFov = 0.41421356F;
  float Aspect = 1.0F;
  float Width = 1.0F;
  float Height = 1.0F;
  DirectX::XMFLOAT4 CameraRight{};
  DirectX::XMFLOAT4 CameraUp{};
  DirectX::XMFLOAT4 CameraForward{};
};
static_assert(sizeof(AmbientOcclusionConstants) == 80);
} // namespace

CameraAmbientOcclusionPass::CameraAmbientOcclusionPass(std::shared_ptr<AmbientOcclusionResources> resources)
    : m_Resources(std::move(resources)) {}

void CameraAmbientOcclusionPass::Setup(RGBuilder& builder, FrameBlackboard& blackboard) {
  m_Views.clear();
  for (RenderView* view : blackboard.GetSceneViews()) {
    if (view == nullptr || !Engine::Ecs::HasScreenSpaceAmbientOcclusion(view->PostProcessing) ||
        !view->AmbientOcclusionDepth.isValid() || !view->AmbientOcclusionNormal.isValid()) {
      continue;
    }
    builder.Read(view->AmbientOcclusionDepth, nvrhi::ResourceStates::ShaderResource);
    builder.Read(view->AmbientOcclusionNormal, nvrhi::ResourceStates::ShaderResource);
    view->AmbientOcclusionTexture = builder.CreateAndWriteTexture({.width = view->ViewportWidth,
                                                                   .height = view->ViewportHeight,
                                                                   .arraySize = 1,
                                                                   .mipLevels = 1,
                                                                   .isRenderTarget = true,
                                                                   .isShaderResource = true,
                                                                   .isUnorderedAccess = false,
                                                                   .format = nvrhi::Format::R32_FLOAT,
                                                                   .dimension = nvrhi::TextureDimension::Texture2D});
    m_Views.push_back({.View = view,
                       .Depth = view->AmbientOcclusionDepth,
                       .Normal = view->AmbientOcclusionNormal,
                       .Output = view->AmbientOcclusionTexture});
  }
}

auto CameraAmbientOcclusionPass::Execute(nvrhi::ICommandList* cmd, const RenderContext& ctx) -> bool {
  if (m_Views.empty()) {
    return true;
  }
  if (cmd == nullptr || m_Resources == nullptr || m_Resources->Device == nullptr ||
      m_Resources->VertexShader == nullptr || m_Resources->PixelShader == nullptr ||
      m_Resources->BindingLayout == nullptr || m_Resources->ConstantsBuffer == nullptr) {
    return false;
  }

  for (const ViewResources& viewResources : m_Views) {
    RenderView* view = viewResources.View;
    nvrhi::TextureHandle depth = ctx.GetPhysicalTexture(viewResources.Depth);
    nvrhi::TextureHandle normal = ctx.GetPhysicalTexture(viewResources.Normal);
    nvrhi::TextureHandle output = ctx.GetPhysicalTexture(viewResources.Output);
    if (view == nullptr || depth == nullptr || normal == nullptr || output == nullptr ||
        !view->PostProcessing.AmbientOcclusion) {
      return false;
    }
    nvrhi::FramebufferDesc framebufferDesc;
    framebufferDesc.addColorAttachment(output);
    nvrhi::FramebufferHandle framebuffer = m_Resources->Device->createFramebuffer(framebufferDesc);
    if (framebuffer == nullptr) {
      return false;
    }
    if (m_Resources->Pipeline == nullptr) {
      nvrhi::GraphicsPipelineDesc pipelineDesc;
      pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
      pipelineDesc.VS = m_Resources->VertexShader;
      pipelineDesc.PS = m_Resources->PixelShader;
      pipelineDesc.bindingLayouts = {m_Resources->BindingLayout};
      pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
      pipelineDesc.renderState.depthStencilState.depthWriteEnable = false;
      pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
      m_Resources->Pipeline =
          m_Resources->Device->createGraphicsPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
      if (m_Resources->Pipeline == nullptr) {
        return false;
      }
    }
    nvrhi::BindingSetDesc bindingDesc;
    bindingDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, depth));
    bindingDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(1, normal));
    bindingDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_Resources->ConstantsBuffer));
    nvrhi::BindingSetHandle bindings = m_Resources->Device->createBindingSet(bindingDesc, m_Resources->BindingLayout);
    if (bindings == nullptr) {
      return false;
    }
    const auto& settings = *view->PostProcessing.AmbientOcclusion;
    const AmbientOcclusionConstants constants{
        .NearPlane = view->CameraNearPlane,
        .FarPlane = view->CameraFarPlane,
        .Radius = settings.Radius,
        .Intensity = settings.Intensity,
        .TanHalfFov = std::tan(view->CameraVerticalFovRadians * 0.5F),
        .Aspect = view->CameraAspectRatio,
        .Width = static_cast<float>(view->ViewportWidth),
        .Height = static_cast<float>(view->ViewportHeight),
        .CameraRight = {view->CameraRight.x, view->CameraRight.y, view->CameraRight.z, 0.0F},
        .CameraUp = {view->CameraUp.x, view->CameraUp.y, view->CameraUp.z, 0.0F},
        .CameraForward = {view->CameraForward.x, view->CameraForward.y, view->CameraForward.z, 0.0F},
    };
    cmd->writeBuffer(m_Resources->ConstantsBuffer, &constants, sizeof(constants));
    ctx.GetFrameContext().GetDiagnostics().UploadBytes += sizeof(constants);
    const nvrhi::FramebufferInfoEx& info = framebuffer->getFramebufferInfo();
    nvrhi::GraphicsState state;
    state.pipeline = m_Resources->Pipeline;
    state.framebuffer = framebuffer;
    state.viewport.addViewport(nvrhi::Viewport(static_cast<float>(info.width), static_cast<float>(info.height)));
    state.viewport.addScissorRect(nvrhi::Rect(static_cast<int>(info.width), static_cast<int>(info.height)));
    state.bindings = {bindings};
    cmd->setGraphicsState(state);
    nvrhi::DrawArguments arguments;
    arguments.vertexCount = 3;
    arguments.instanceCount = 1;
    cmd->draw(arguments);
    ++ctx.GetFrameContext().GetDiagnostics().DrawCalls;
  }
  return true;
}

namespace {
struct CameraPostProcessConstants {
  uint32_t EffectType = 0;
  float Exposure = 0.0F;
  float Contrast = 1.0F;
  float Saturation = 1.0F;
  float Threshold = 1.0F;
  float Intensity = 0.25F;
  float Radius = 4.0F;
  float Padding = 0.0F;
};

auto ToConstants(const Engine::Ecs::CameraPostProcessEffect& effect) -> CameraPostProcessConstants {
  if (const auto* grading = std::get_if<Engine::Ecs::ColorGradingEffect>(&effect)) {
    return {.EffectType = 0,
            .Exposure = grading->Exposure,
            .Contrast = grading->Contrast,
            .Saturation = grading->Saturation};
  }
  const auto& bloom = std::get<Engine::Ecs::BloomEffect>(effect);
  return {.EffectType = 1, .Threshold = bloom.Threshold, .Intensity = bloom.Intensity, .Radius = bloom.Radius};
}

auto DescribeColorTexture(uint32_t width, uint32_t height) -> RGTextureDesc {
  return {.width = width,
          .height = height,
          .arraySize = 1,
          .mipLevels = 1,
          .isRenderTarget = true,
          .isShaderResource = true,
          .isUnorderedAccess = false,
          .format = nvrhi::Format::RGBA32_FLOAT,
          .dimension = nvrhi::TextureDimension::Texture2D};
}

auto DrawCameraPostProcess(nvrhi::ICommandList* cmd, const RenderContext& ctx,
                           const std::shared_ptr<CameraPostProcessResources>& resources,
                           const nvrhi::TextureHandle& input, const nvrhi::TextureHandle& output,
                           nvrhi::FramebufferHandle& framebuffer, const CameraPostProcessConstants& constants) -> bool {
  if (cmd == nullptr || resources == nullptr || resources->Device == nullptr || resources->VertexShader == nullptr ||
      resources->PixelShader == nullptr || resources->BindingLayout == nullptr ||
      resources->ConstantsBuffer == nullptr || input == nullptr || output == nullptr) {
    return false;
  }
  nvrhi::FramebufferDesc framebufferDesc;
  framebufferDesc.addColorAttachment(output);
  framebuffer = resources->Device->createFramebuffer(framebufferDesc);
  if (framebuffer == nullptr) {
    return false;
  }
  if (resources->Pipeline == nullptr) {
    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
    pipelineDesc.VS = resources->VertexShader;
    pipelineDesc.PS = resources->PixelShader;
    pipelineDesc.bindingLayouts = {resources->BindingLayout};
    pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
    pipelineDesc.renderState.depthStencilState.depthWriteEnable = false;
    pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    resources->Pipeline = resources->Device->createGraphicsPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
    if (resources->Pipeline == nullptr) {
      return false;
    }
  }
  nvrhi::BindingSetDesc bindingDesc;
  bindingDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, input));
  bindingDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, resources->ConstantsBuffer));
  nvrhi::BindingSetHandle bindings = resources->Device->createBindingSet(bindingDesc, resources->BindingLayout);
  if (bindings == nullptr) {
    return false;
  }
  cmd->writeBuffer(resources->ConstantsBuffer, &constants, sizeof(constants));
  ctx.GetFrameContext().GetDiagnostics().UploadBytes += sizeof(constants);
  const nvrhi::FramebufferInfoEx& framebufferInfo = framebuffer->getFramebufferInfo();
  nvrhi::GraphicsState state;
  state.pipeline = resources->Pipeline;
  state.framebuffer = framebuffer;
  state.viewport.addViewport(
      nvrhi::Viewport(static_cast<float>(framebufferInfo.width), static_cast<float>(framebufferInfo.height)));
  state.viewport.addScissorRect(
      nvrhi::Rect(static_cast<int>(framebufferInfo.width), static_cast<int>(framebufferInfo.height)));
  state.bindings = {bindings};
  cmd->setGraphicsState(state);
  nvrhi::DrawArguments arguments;
  arguments.vertexCount = 3;
  arguments.instanceCount = 1;
  cmd->draw(arguments);
  ++ctx.GetFrameContext().GetDiagnostics().DrawCalls;
  return true;
}
} // namespace

CameraPostProcessPass::CameraPostProcessPass(std::shared_ptr<CameraPostProcessResources> resources, size_t effectIndex)
    : m_Resources(std::move(resources)), m_EffectIndex(effectIndex) {}

void CameraPostProcessPass::Setup(RGBuilder& builder, FrameBlackboard& blackboard) {
  m_Views.clear();
  for (RenderView* view : blackboard.GetSceneViews()) {
    if (view == nullptr || view->PostProcessing.Effects.size() <= m_EffectIndex || view->OutputAttachments.empty()) {
      continue;
    }
    const RGResourceID originalColor = view->OutputAttachments.front().Resource;
    const RGResourceID input = m_EffectIndex == 0 ? originalColor : view->PostProcessColor;
    const RGResourceID output =
        builder.CreateAndWriteTexture(DescribeColorTexture(view->ViewportWidth, view->ViewportHeight));
    builder.Read(input);
    view->PostProcessColor = output;
    m_Views.push_back(
        {.View = view, .Input = input, .Output = output, .Effect = view->PostProcessing.Effects[m_EffectIndex]});
  }
}

auto CameraPostProcessPass::Execute(nvrhi::ICommandList* cmd, const RenderContext& ctx) -> bool {
  if (m_Views.empty()) {
    return true;
  }
  for (const ViewResources& viewResources : m_Views) {
    nvrhi::TextureHandle input = ctx.GetPhysicalTexture(viewResources.Input);
    nvrhi::TextureHandle output = ctx.GetPhysicalTexture(viewResources.Output);
    nvrhi::FramebufferHandle framebuffer;
    if (viewResources.View == nullptr || input == nullptr || output == nullptr ||
        !DrawCameraPostProcess(cmd, ctx, m_Resources, input, output, framebuffer, ToConstants(viewResources.Effect))) {
      return false;
    }
  }
  return true;
}

CameraPostProcessCompositePass::CameraPostProcessCompositePass(std::shared_ptr<CameraPostProcessResources> resources)
    : m_Resources(std::move(resources)) {}

void CameraPostProcessCompositePass::Setup(RGBuilder& builder, FrameBlackboard& blackboard) {
  m_Views.clear();
  for (RenderView* view : blackboard.GetSceneViews()) {
    if (view == nullptr || view->PostProcessing.Effects.empty() || view->OutputAttachments.empty() ||
        !view->PostProcessColor.isValid()) {
      continue;
    }
    const RGResourceID color = view->OutputAttachments.front().Resource;
    builder.Read(view->PostProcessColor);
    builder.Write(color, nvrhi::ResourceStates::RenderTarget);
    m_Views.push_back({.View = view, .Input = view->PostProcessColor, .Output = color});
  }
}

auto CameraPostProcessCompositePass::Execute(nvrhi::ICommandList* cmd, const RenderContext& ctx) -> bool {
  for (const ViewResources& viewResources : m_Views) {
    nvrhi::TextureHandle input = ctx.GetPhysicalTexture(viewResources.Input);
    nvrhi::TextureHandle output = ctx.GetPhysicalTexture(viewResources.Output);
    nvrhi::FramebufferHandle framebuffer;
    const CameraPostProcessConstants copyConstants{.EffectType = 2};
    if (viewResources.View == nullptr || input == nullptr || output == nullptr ||
        !DrawCameraPostProcess(cmd, ctx, m_Resources, input, output, framebuffer, copyConstants)) {
      return false;
    }
  }
  return true;
}

DisplayTransformPass::DisplayTransformPass(std::shared_ptr<DisplayTransformResources> resources)
    : m_Resources(std::move(resources)) {}

void DisplayTransformPass::Setup(::Engine::Renderer::RGBuilder& builder, FrameBlackboard& blackboard) {
  m_Views.clear();
  for (RenderView* view : blackboard.GetSceneViews()) {
    if (view == nullptr || view->RenderMode == Engine::ViewportBinding::Mode::Final ||
        view->DisplayFramebuffer == nullptr || view->DisplayColorTexture == nullptr ||
        view->OutputAttachments.empty()) {
      continue;
    }

    view->DisplayColorResource = builder.ImportTexture(
        {.width = view->DisplayColorTexture->getDesc().width,
         .height = view->DisplayColorTexture->getDesc().height,
         .arraySize = view->DisplayColorTexture->getDesc().arraySize,
         .mipLevels = view->DisplayColorTexture->getDesc().mipLevels,
         .isRenderTarget = view->DisplayColorTexture->getDesc().isRenderTarget,
         .isShaderResource = view->DisplayColorTexture->getDesc().isShaderResource,
         .isUnorderedAccess = view->DisplayColorTexture->getDesc().isUAV,
         .format = view->DisplayColorTexture->getDesc().format,
         .dimension = view->DisplayColorTexture->getDesc().dimension},
        view->DisplayColorTexture, nvrhi::ResourceStates::RenderTarget, nvrhi::ResourceStates::ShaderResource);
    const RGResourceID sceneColor = view->OutputAttachments.front().Resource;
    builder.Read(sceneColor, nvrhi::ResourceStates::ShaderResource);
    builder.Write(view->DisplayColorResource, nvrhi::ResourceStates::RenderTarget);
    m_Views.push_back({.View = view, .SceneColor = sceneColor, .DisplayColor = view->DisplayColorResource});
  }
}

auto DisplayTransformPass::Execute(nvrhi::ICommandList* cmd, const RenderContext& ctx) -> bool {
  if (m_Views.empty()) {
    return true;
  }
  if (cmd == nullptr || m_Resources == nullptr || m_Resources->Device == nullptr ||
      m_Resources->VertexShader == nullptr || m_Resources->PixelShader == nullptr ||
      m_Resources->BindingLayout == nullptr) {
    return false;
  }

  for (const ViewResources& viewResources : m_Views) {
    RenderView* view = viewResources.View;
    nvrhi::TextureHandle sceneColor = ctx.GetPhysicalTexture(viewResources.SceneColor);
    nvrhi::TextureHandle displayColor = ctx.GetPhysicalTexture(viewResources.DisplayColor);
    if (view == nullptr || sceneColor == nullptr || displayColor == nullptr || view->DisplayFramebuffer == nullptr) {
      return false;
    }

    if (m_Resources->Pipeline == nullptr) {
      nvrhi::GraphicsPipelineDesc pipelineDesc;
      pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
      pipelineDesc.VS = m_Resources->VertexShader;
      pipelineDesc.PS = m_Resources->PixelShader;
      pipelineDesc.bindingLayouts = {m_Resources->BindingLayout};
      pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
      pipelineDesc.renderState.depthStencilState.depthWriteEnable = false;
      pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
      m_Resources->Pipeline =
          m_Resources->Device->createGraphicsPipeline(pipelineDesc, view->DisplayFramebuffer->getFramebufferInfo());
      if (m_Resources->Pipeline == nullptr) {
        return false;
      }
    }

    nvrhi::BindingSetDesc bindingDesc;
    bindingDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, sceneColor));
    nvrhi::BindingSetHandle bindings = m_Resources->Device->createBindingSet(bindingDesc, m_Resources->BindingLayout);
    if (bindings == nullptr) {
      return false;
    }
    const nvrhi::FramebufferInfoEx& framebufferInfo = view->DisplayFramebuffer->getFramebufferInfo();
    nvrhi::GraphicsState state;
    state.pipeline = m_Resources->Pipeline;
    state.framebuffer = view->DisplayFramebuffer;
    state.viewport.addViewport(
        nvrhi::Viewport(static_cast<float>(framebufferInfo.width), static_cast<float>(framebufferInfo.height)));
    state.viewport.addScissorRect(
        nvrhi::Rect(static_cast<int>(framebufferInfo.width), static_cast<int>(framebufferInfo.height)));
    state.bindings = {bindings};
    cmd->setGraphicsState(state);
    nvrhi::DrawArguments arguments;
    arguments.vertexCount = 3;
    arguments.instanceCount = 1;
    cmd->draw(arguments);
    ++ctx.GetFrameContext().GetDiagnostics().DrawCalls;
  }
  return true;
}
} // namespace Engine::Renderer

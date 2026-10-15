#include "rendering/src/render_graph/RenderPass.h"
#include "rendering/src/SceneRenderer.h"
#include "rendering/src/render_graph/FrameBlackboard.h"
#include "rendering/src/render_graph/RenderView.h"
#include <algorithm>

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
        (view->LightBuffer == nullptr)) {
      return false;
    }
    ::Engine::Renderer::LightsCBData lightData{};
    const uint32_t lightCount =
        static_cast<uint32_t>(std::min(view->Lights.size(), static_cast<size_t>(ENGINE_MAX_LIGHTS)));
    for (uint32_t i = 0; i < lightCount; ++i) {
      lightData.Lights[i] = view->Lights.at(i);
    }
    lightData.LightCount = lightCount;
    cmd->writeBuffer(view->LightBuffer, &lightData, sizeof(lightData));
    ctx.GetFrameContext().GetDiagnostics().UploadBytes += sizeof(lightData);
    nvrhi::BindingSetHandle sceneBindings =
        (m_SceneRenderer != nullptr)
            ? m_SceneRenderer->GetSceneBindingSet(viewIndex, view->LightBuffer, physicalShadowMap, shadowSampler,
                                                  view->CameraBuffer, ctx.GetFrameContext().GetDiagnostics())
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
} // namespace Engine::Renderer

#include "rendering/src/render_graph/RenderPass.h"
#include "rendering/src/SceneRenderer.h"
#include "rendering/src/render_graph/FrameBlackboard.h"
#include "rendering/src/render_graph/RenderView.h"
#include <algorithm>
#include <limits>

namespace
{
constexpr uint32_t ShadowMapSize = 2048;
constexpr uint32_t MaxShadowedLights = 4;
int ToScissorDimension(uint32_t dimension)
{
    return static_cast<int>((std::min)(dimension, static_cast<uint32_t>(std::numeric_limits<int>::max())));
}
} // namespace

namespace Engine::Renderer
{

// ==============================================================================
// SHADOW PASS
// ==============================================================================

ShadowPass::ShadowPass() = default;

void ShadowPass::Setup(::Engine::Renderer::RGBuilder &builder,
                       ::Engine::Renderer::FrameBlackboard &blackboard)
{
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
    const auto &shadowViews = blackboard.GetShadowViews();
    if (shadowViews.empty())
    {
        builder.Write(m_ShadowMapID, nvrhi::ResourceStates::DepthWrite);
    }
    for (uint32_t shadowSlot = 0; shadowSlot < shadowViews.size(); ++shadowSlot)
    {
        nvrhi::TextureSubresourceSet subresources;
        subresources.baseArraySlice = shadowSlot;
        subresources.numArraySlices = 1;
        subresources.baseMipLevel = 0;
        subresources.numMipLevels = 1;
        builder.Write(m_ShadowMapID, nvrhi::ResourceStates::DepthWrite, subresources);
    }
    blackboard.SetShadowMap(m_ShadowMapID);
}

bool ShadowPass::Execute(nvrhi::ICommandList *cmd, const ::Engine::Renderer::RenderContext &ctx)
{
    const auto &shadowViews = ctx.GetBlackboard().GetShadowViews();
    if (shadowViews.empty())
        return true;
    nvrhi::BufferHandle perObjectBuffer = ctx.GetBlackboard().GetPerObjectBuffer();
    nvrhi::BindingSetHandle perObjectBindings = ctx.GetBlackboard().GetPerObjectBindingSet();
    if (!perObjectBuffer || !perObjectBindings)
        return false;

    nvrhi::TextureHandle physicalShadowMap = ctx.GetPhysicalTexture(m_ShadowMapID);
    if (!physicalShadowMap)
        return false;

    const nvrhi::TextureDesc &shadowTexDesc = physicalShadowMap->getDesc();

    nvrhi::Viewport shadowViewport = nvrhi::Viewport(float(shadowTexDesc.width), float(shadowTexDesc.height));
    nvrhi::Rect shadowScissor =
        nvrhi::Rect(ToScissorDimension(shadowTexDesc.width), ToScissorDimension(shadowTexDesc.height));

    if (!m_ShadowMapTexture || m_ShadowMapTexture.Get() != physicalShadowMap.Get())
    {
        m_ShadowMapTexture = physicalShadowMap;
        m_ShadowFramebuffers.clear();
    }

    for (uint32_t shadowSlot = 0; shadowSlot < shadowViews.size(); ++shadowSlot)
    {
        ::Engine::Renderer::RenderView *view = shadowViews[shadowSlot];
        if (!view)
            continue;

        if (shadowSlot >= m_ShadowFramebuffers.size())
        {
            m_ShadowFramebuffers.resize(shadowSlot + 1);
        }

        if (!m_ShadowFramebuffers[shadowSlot])
        {
            nvrhi::FramebufferDesc desc;
            desc.depthAttachment.texture = physicalShadowMap;
            desc.depthAttachment.subresources.baseArraySlice = shadowSlot;
            desc.depthAttachment.subresources.numArraySlices = 1;
            desc.depthAttachment.subresources.baseMipLevel = 0;
            desc.depthAttachment.subresources.numMipLevels = 1;
            m_ShadowFramebuffers[shadowSlot] = ctx.GetDevice()->createFramebuffer(desc);
        }
        nvrhi::IFramebuffer *shadowFb = m_ShadowFramebuffers[shadowSlot];
        if (!shadowFb)
            return false;

        nvrhi::TextureSubresourceSet subresources;
        subresources.baseArraySlice = shadowSlot;
        subresources.numArraySlices = 1;
        subresources.baseMipLevel = 0;
        subresources.numMipLevels = 1;
        cmd->clearDepthStencilTexture(physicalShadowMap, subresources, true, 1.0f, false, 0);

        for (const auto &packet : view->Queues.Opaque)
        {
            if (!packet.Pipeline || !packet.VertexBuffer ||
                (packet.ShadowAlphaTest && !packet.MaterialBindings))
                return false;
            cmd->writeBuffer(perObjectBuffer, &packet.PerObjectData, sizeof(packet.PerObjectData));
            ctx.GetFrameContext().GetDiagnostics().UploadBytes += sizeof(packet.PerObjectData);
            ++ctx.GetFrameContext().GetDiagnostics().DrawCalls;
            nvrhi::GraphicsState state;
            state.pipeline = packet.Pipeline;
            state.framebuffer = shadowFb;
            state.viewport.addViewport(shadowViewport);
            state.viewport.addScissorRect(shadowScissor);

            state.bindings = {perObjectBindings};
            if (packet.ShadowAlphaTest)
                state.bindings.push_back(packet.MaterialBindings);

            nvrhi::VertexBufferBinding vbb;
            vbb.buffer = packet.VertexBuffer;
            vbb.slot = 0;
            vbb.offset = 0;
            state.vertexBuffers = {vbb};

            if (packet.IndexBuffer)
            {
                state.indexBuffer.buffer = packet.IndexBuffer;
                state.indexBuffer.format = nvrhi::Format::R32_UINT;
                state.indexBuffer.offset = 0;
            }

            cmd->setGraphicsState(state);

            nvrhi::DrawArguments args;
            args.instanceCount = 1;
            args.startVertexLocation = packet.BaseVertexOffset;
            if (packet.IndexBuffer)
            {
                args.startIndexLocation = packet.StartIndexLocation;
                args.vertexCount = packet.IndexCount;
                cmd->drawIndexed(args);
            }
            else
            {
                args.vertexCount = packet.VertexCount;
                cmd->draw(args);
            }
        }
    }
    return true;
}

// ==============================================================================
// FORWARD PASS
// ==============================================================================

ForwardPass::ForwardPass(Engine::SceneRenderer *sceneRenderer) : m_SceneRenderer(sceneRenderer)
{
}

void ForwardPass::Setup(::Engine::Renderer::RGBuilder &builder, FrameBlackboard &blackboard)
{
    m_ShadowMapInputID = blackboard.GetShadowMap();
    builder.Read(m_ShadowMapInputID);
    for (::Engine::Renderer::RenderView *view : blackboard.GetSceneViews())
    {
        if (!view || !view->TargetFramebuffer)
            continue;
        view->OutputAttachments.clear();
        const nvrhi::FramebufferDesc &framebuffer = view->TargetFramebuffer->getDesc();
        auto importAttachment = [&](const nvrhi::FramebufferAttachment &attachment, bool depth) {
            if (!attachment.texture)
                return;
            const nvrhi::TextureDesc &texture = attachment.texture->getDesc();
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
            const ::Engine::Renderer::RGResourceID id = builder.ImportTexture(
                desc, nvrhi::TextureHandle(attachment.texture), initialState, finalState);
            builder.Write(id, writeState, attachment.subresources);
            ::Engine::Renderer::RGViewportAttachment output;
            output.Resource = id;
            output.Subresources = attachment.subresources;
            output.IsDepth = depth;
            view->OutputAttachments.push_back(output);
        };
        for (const nvrhi::FramebufferAttachment &attachment : framebuffer.colorAttachments)
        {
            importAttachment(attachment, false);
        }
        if (framebuffer.depthAttachment.valid())
            importAttachment(framebuffer.depthAttachment, true);
    }
}

bool ForwardPass::Execute(nvrhi::ICommandList *cmd, const ::Engine::Renderer::RenderContext &ctx)
{
    const auto &sceneViews = ctx.GetBlackboard().GetSceneViews();
    if (sceneViews.empty())
        return true;

    nvrhi::TextureHandle physicalShadowMap = ctx.GetPhysicalTexture(m_ShadowMapInputID);
    nvrhi::BufferHandle perObjectBuffer = ctx.GetBlackboard().GetPerObjectBuffer();
    nvrhi::BindingSetHandle perObjectBindings = ctx.GetBlackboard().GetPerObjectBindingSet();
    nvrhi::SamplerHandle shadowSampler = ctx.GetBlackboard().GetShadowSampler();
    nvrhi::BindingLayoutHandle sceneBindingLayout = ctx.GetBlackboard().GetSceneBindingLayout();
    if (!physicalShadowMap || !perObjectBuffer || !perObjectBindings || !shadowSampler || !sceneBindingLayout)
        return false;

    auto drawQueue = [&](const std::vector<::Engine::Renderer::RenderPacket> &packets,
                         const nvrhi::BindingSetHandle &sceneBindings, nvrhi::IFramebuffer *fb,
                         const nvrhi::Viewport &vp, const nvrhi::Rect &scissor) {
        for (const auto &packet : packets)
        {
            if (!packet.Pipeline || !packet.VertexBuffer || !sceneBindings)
                return false;
            cmd->writeBuffer(perObjectBuffer, &packet.PerObjectData, sizeof(packet.PerObjectData));
            ctx.GetFrameContext().GetDiagnostics().UploadBytes += sizeof(packet.PerObjectData);
            nvrhi::GraphicsState state;
            state.pipeline = packet.Pipeline;
            state.framebuffer = fb;
            state.viewport.addViewport(vp);
            state.viewport.addScissorRect(scissor);

            state.bindings = {perObjectBindings, sceneBindings, packet.MaterialBindings};

            nvrhi::VertexBufferBinding vbb;
            vbb.buffer = packet.VertexBuffer;
            vbb.slot = 0;
            vbb.offset = 0;
            state.vertexBuffers = {vbb};

            if (packet.IndexBuffer)
            {
                state.indexBuffer.buffer = packet.IndexBuffer;
                state.indexBuffer.format = nvrhi::Format::R32_UINT;
                state.indexBuffer.offset = 0;
            }

            cmd->setGraphicsState(state);

            nvrhi::DrawArguments args;
            args.instanceCount = 1;
            args.startVertexLocation = packet.BaseVertexOffset;
            if (packet.IndexBuffer)
            {
                args.startIndexLocation = packet.StartIndexLocation;
                args.vertexCount = packet.IndexCount;
                cmd->drawIndexed(args);
            }
            else
            {
                args.vertexCount = packet.VertexCount;
                cmd->draw(args);
            }
            ++ctx.GetFrameContext().GetDiagnostics().DrawCalls;
        }
        return true;
    };

    for (size_t viewIndex = 0; viewIndex < sceneViews.size(); ++viewIndex)
    {
        ::Engine::Renderer::RenderView *view = sceneViews[viewIndex];
        if (!view || !view->TargetFramebuffer || !view->CameraBuffer || !view->LightBuffer)
            return false;
        ::Engine::Renderer::LightsCBData lightData{};
        const uint32_t lightCount =
            static_cast<uint32_t>(std::min(view->Lights.size(), static_cast<size_t>(ENGINE_MAX_LIGHTS)));
        for (uint32_t i = 0; i < lightCount; ++i)
            lightData.Lights[i] = view->Lights[i];
        lightData.LightCount = lightCount;
        cmd->writeBuffer(view->LightBuffer, &lightData, sizeof(lightData));
        ctx.GetFrameContext().GetDiagnostics().UploadBytes += sizeof(lightData);
        nvrhi::BindingSetHandle sceneBindings =
            m_SceneRenderer
                ? m_SceneRenderer->GetSceneBindingSet(viewIndex, view->LightBuffer, physicalShadowMap,
                                                      shadowSampler, view->CameraBuffer,
                                                      ctx.GetFrameContext().GetDiagnostics())
                : nullptr;
        if (!sceneBindings)
            return false;

        nvrhi::IFramebuffer *mainFramebuffer = view->TargetFramebuffer;
        for (const ::Engine::Renderer::RGViewportAttachment &output : view->OutputAttachments)
        {
            if (output.Load != ::Engine::Renderer::RGAttachmentLoadOp::Clear)
                continue;
            nvrhi::TextureHandle texture = ctx.GetPhysicalTexture(output.Resource);
            if (!texture)
                return false;
            if (output.IsDepth)
            {
                cmd->clearDepthStencilTexture(texture, output.Subresources, true, 1.0f, false, 0);
            }
            else
            {
                cmd->clearTextureFloat(texture, output.Subresources, output.ClearColor);
            }
        }

        const nvrhi::FramebufferInfoEx &fbInfo = mainFramebuffer->getFramebufferInfo();

        nvrhi::Viewport viewportState = nvrhi::Viewport(float(fbInfo.width), float(fbInfo.height));
        nvrhi::Rect scissor =
            nvrhi::Rect(ToScissorDimension(fbInfo.width), ToScissorDimension(fbInfo.height));

        if (!drawQueue(view->Queues.Opaque, sceneBindings, mainFramebuffer, viewportState, scissor) ||
            !drawQueue(view->Queues.Masked, sceneBindings, mainFramebuffer, viewportState, scissor) ||
            !drawQueue(view->Queues.Skybox, sceneBindings, mainFramebuffer, viewportState, scissor) ||
            !drawQueue(view->Queues.Transparent, sceneBindings, mainFramebuffer, viewportState, scissor) ||
            !drawQueue(view->Queues.UI, sceneBindings, mainFramebuffer, viewportState, scissor))
            return false;
    }
    return true;
}

// ==============================================================================
// DEBUG PASS
// ==============================================================================

DebugPass::DebugPass(Engine::SceneRenderer *sceneRenderer) : m_SceneRenderer(sceneRenderer)
{
}

void DebugPass::Setup(::Engine::Renderer::RGBuilder &builder, FrameBlackboard &blackboard)
{
    for (::Engine::Renderer::RenderView *view : blackboard.GetSceneViews())
    {
        if (!view)
            continue;
        for (const ::Engine::Renderer::RGViewportAttachment &output : view->OutputAttachments)
        {
            builder.Write(output.Resource,
                          output.IsDepth ? nvrhi::ResourceStates::DepthWrite
                                         : nvrhi::ResourceStates::RenderTarget,
                          output.Subresources);
        }
    }
}

bool DebugPass::Execute(nvrhi::ICommandList *cmd, const ::Engine::Renderer::RenderContext &ctx)
{
    if (!m_SceneRenderer)
    {
        return false;
    }

    for (const ::Engine::Renderer::RenderView *view : ctx.GetBlackboard().GetSceneViews())
    {
        if (view)
        {
            m_SceneRenderer->RenderDebugGeometry(cmd, *view);
        }
    }
    return true;
}
} // namespace Engine::Renderer

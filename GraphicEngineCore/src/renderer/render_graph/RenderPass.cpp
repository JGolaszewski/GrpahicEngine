#include "renderer/render_graph/RenderPass.h"
#include "renderer/render_graph/FrameBlackboard.h"
#include "renderer/render_graph/RenderView.h"
#include <algorithm>

namespace {
	constexpr uint32_t ShadowMapSize = 2048;
	constexpr uint32_t MaxShadowedLights = 4;
}

namespace Renderer {

	// ==============================================================================
	// SHADOW PASS
	// ==============================================================================

	ShadowPass::ShadowPass() = default;

	void ShadowPass::Setup(Renderer::RGBuilder& builder, Renderer::FrameBlackboard& blackboard) {
		uint32_t shadowMapSize = blackboard.Get<uint32_t>("ShadowMapSize", ShadowMapSize);
		uint32_t maxShadowedLights = blackboard.Get<uint32_t>("MaxShadowedLights", MaxShadowedLights);

		Renderer::RGTextureDesc desc{};
		desc.width = shadowMapSize;
		desc.height = shadowMapSize;
		desc.format = nvrhi::Format::D32;
		desc.arraySize = maxShadowedLights;
		desc.dimension = nvrhi::TextureDimension::Texture2DArray;
		desc.isRenderTarget = true;
		desc.mipLevels = 1;

		m_ShadowMapID = builder.CreateAndWriteTexture(desc);
		blackboard.Set<Renderer::RGResourceID>("ShadowMap", m_ShadowMapID);
	}

	void ShadowPass::Execute(nvrhi::ICommandList* cmd, const Renderer::RenderContext& ctx) {
		const auto& shadowViews = ctx.GetBlackboard().GetShadowViews();
		if (shadowViews.empty()) return;

		nvrhi::TextureHandle physicalShadowMap = ctx.GetPhysicalTexture(m_ShadowMapID);
		if (!physicalShadowMap) return;

		const nvrhi::TextureDesc& shadowTexDesc = physicalShadowMap->getDesc();

		nvrhi::Viewport shadowViewport = nvrhi::Viewport(float(shadowTexDesc.width), float(shadowTexDesc.height));
		nvrhi::Rect shadowScissor = nvrhi::Rect(shadowTexDesc.width, shadowTexDesc.height);

		nvrhi::BufferHandle lightBuffer = ctx.GetBlackboard().GetLightBuffer();
		nvrhi::SamplerHandle shadowSampler = ctx.GetBlackboard().GetShadowSampler();
		nvrhi::BindingLayoutHandle sceneBindingLayout = ctx.GetBlackboard().GetSceneBindingLayout();

		nvrhi::BindingSetHandle sceneBindings;
		if (sceneBindingLayout) {
			nvrhi::BindingSetDesc sceneSetDesc;
			sceneSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(2, lightBuffer));
			sceneSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(3, physicalShadowMap));
			sceneSetDesc.addItem(nvrhi::BindingSetItem::Sampler(3, shadowSampler));
			sceneBindings = ctx.GetDevice()->createBindingSet(sceneSetDesc, sceneBindingLayout);
		}

		for (uint32_t shadowSlot = 0; shadowSlot < shadowViews.size(); ++shadowSlot) {
			Renderer::RenderView* view = shadowViews[shadowSlot];
			if (!view) continue;

			if (shadowSlot >= m_ShadowFramebuffers.size()) {
				m_ShadowFramebuffers.resize(shadowSlot + 1);
			}

			if (!m_ShadowFramebuffers[shadowSlot]) {
				nvrhi::FramebufferDesc desc;
				desc.depthAttachment.texture = physicalShadowMap;
				desc.depthAttachment.subresources.baseArraySlice = shadowSlot;
				desc.depthAttachment.subresources.numArraySlices = 1;
				desc.depthAttachment.subresources.baseMipLevel = 0;
				desc.depthAttachment.subresources.numMipLevels = 1;
				m_ShadowFramebuffers[shadowSlot] = ctx.GetDevice()->createFramebuffer(desc);
			}
			nvrhi::IFramebuffer* shadowFb = m_ShadowFramebuffers[shadowSlot];

			nvrhi::TextureSubresourceSet subresources;
			subresources.baseArraySlice = shadowSlot;
			subresources.numArraySlices = 1;
			subresources.baseMipLevel = 0;
			subresources.numMipLevels = 1;
			cmd->clearDepthStencilTexture(physicalShadowMap, subresources, true, 1.0f, false, 0);

			for (const auto& packet : view->Queues.Opaque) {
				nvrhi::GraphicsState state;
				state.pipeline = packet.Pipeline;
				state.framebuffer = shadowFb;
				state.viewport.addViewport(shadowViewport);
				state.viewport.addScissorRect(shadowScissor);

				state.bindings = { packet.PerObjectBindings, sceneBindings, packet.MaterialBindings };

				nvrhi::VertexBufferBinding vbb;
				vbb.buffer = packet.VertexBuffer;
				vbb.slot = 0;
				vbb.offset = 0;
				state.vertexBuffers = { vbb };

				if (packet.IndexBuffer) {
					state.indexBuffer.buffer = packet.IndexBuffer;
					state.indexBuffer.format = nvrhi::Format::R32_UINT;
					state.indexBuffer.offset = 0;
				}

				cmd->setGraphicsState(state);

				nvrhi::DrawArguments args;
				args.instanceCount = 1;
				args.startVertexLocation = packet.BaseVertexOffset;
				if (packet.IndexBuffer) {
					args.startIndexLocation = packet.StartIndexLocation;
					args.vertexCount = packet.IndexCount;
					cmd->drawIndexed(args);
				}
				else {
					args.vertexCount = packet.VertexCount;
					cmd->draw(args);
				}
			}
		}
	}

	// ==============================================================================
	// FORWARD PASS
	// ==============================================================================

	ForwardPass::ForwardPass() = default;

	void ForwardPass::Setup(Renderer::RGBuilder& builder, FrameBlackboard& blackboard) {
		m_ShadowMapInputID = blackboard.Get<Renderer::RGResourceID>("ShadowMap", Renderer::RGResourceID());
		builder.Read(m_ShadowMapInputID);
	}

	void ForwardPass::Execute(nvrhi::ICommandList* cmd, const Renderer::RenderContext& ctx) {
		const auto& sceneViews = ctx.GetBlackboard().GetSceneViews();
		if (sceneViews.empty()) return;

		const auto& lights = ctx.GetBlackboard().GetDirectionalLights();

		nvrhi::TextureHandle physicalShadowMap = ctx.GetPhysicalTexture(m_ShadowMapInputID);
		nvrhi::BufferHandle lightBuffer = ctx.GetBlackboard().GetLightBuffer();
		nvrhi::SamplerHandle shadowSampler = ctx.GetBlackboard().GetShadowSampler();
		nvrhi::BindingLayoutHandle sceneBindingLayout = ctx.GetBlackboard().GetSceneBindingLayout();

		Renderer::LightsCBData lightData{};
		const uint32_t lightCount = static_cast<uint32_t>(std::min(lights.size(), static_cast<size_t>(ENGINE_MAX_LIGHTS)));
		for (uint32_t i = 0; i < lightCount; ++i) {
			lightData.Lights[i] = lights[i];
		}
		lightData.LightCount = lightCount;
		cmd->writeBuffer(lightBuffer, &lightData, sizeof(LightsCBData));

		nvrhi::BindingSetHandle sceneBindings;
		if (sceneBindingLayout) {
			nvrhi::BindingSetDesc sceneSetDesc;
			sceneSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(2, lightBuffer));
			sceneSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(3, physicalShadowMap));
			sceneSetDesc.addItem(nvrhi::BindingSetItem::Sampler(3, shadowSampler));
			sceneBindings = ctx.GetDevice()->createBindingSet(sceneSetDesc, sceneBindingLayout);
		}

		auto drawQueue = [&](const std::vector<Renderer::RenderPacket>& packets, nvrhi::IFramebuffer* fb, const nvrhi::Viewport& vp, const nvrhi::Rect& scissor) {
			for (const auto& packet : packets) {
				nvrhi::GraphicsState state;
				state.pipeline = packet.Pipeline;
				state.framebuffer = fb;
				state.viewport.addViewport(vp);
				state.viewport.addScissorRect(scissor);

				state.bindings = { packet.PerObjectBindings, sceneBindings, packet.MaterialBindings };

				nvrhi::VertexBufferBinding vbb;
				vbb.buffer = packet.VertexBuffer;
				vbb.slot = 0;
				vbb.offset = 0;
				state.vertexBuffers = { vbb };

				if (packet.IndexBuffer) {
					state.indexBuffer.buffer = packet.IndexBuffer;
					state.indexBuffer.format = nvrhi::Format::R32_UINT;
					state.indexBuffer.offset = 0;
				}

				cmd->setGraphicsState(state);

				nvrhi::DrawArguments args;
				args.instanceCount = 1;
				args.startVertexLocation = packet.BaseVertexOffset;
				if (packet.IndexBuffer) {
					args.startIndexLocation = packet.StartIndexLocation;
					args.vertexCount = packet.IndexCount;
					cmd->drawIndexed(args);
				}
				else {
					args.vertexCount = packet.VertexCount;
					cmd->draw(args);
				}
			}
			};

		for (Renderer::RenderView* view : sceneViews) {
			if (!view || !view->TargetFramebuffer) continue;

			nvrhi::IFramebuffer* mainFramebuffer = view->TargetFramebuffer;

			const nvrhi::FramebufferDesc& fbDesc = mainFramebuffer->getDesc();
			if (!fbDesc.colorAttachments.empty() && fbDesc.colorAttachments[0].texture) {
				cmd->clearTextureFloat(fbDesc.colorAttachments[0].texture, nvrhi::AllSubresources, nvrhi::Color(0.15f, 0.15f, 0.15f, 1.0f));
			}
			if (fbDesc.depthAttachment.valid() && fbDesc.depthAttachment.texture) {
				cmd->clearDepthStencilTexture(fbDesc.depthAttachment.texture, nvrhi::AllSubresources, true, 1.0f, false, 0);
			}

			const nvrhi::FramebufferInfoEx& fbInfo = mainFramebuffer->getFramebufferInfo();

			nvrhi::Viewport viewportState = nvrhi::Viewport(float(fbInfo.width), float(fbInfo.height));
			nvrhi::Rect scissor = nvrhi::Rect(fbInfo.width, fbInfo.height);

			drawQueue(view->Queues.Opaque, mainFramebuffer, viewportState, scissor);
			drawQueue(view->Queues.Masked, mainFramebuffer, viewportState, scissor);
			drawQueue(view->Queues.Skybox, mainFramebuffer, viewportState, scissor);
			drawQueue(view->Queues.Transparent, mainFramebuffer, viewportState, scissor);
			drawQueue(view->Queues.UI, mainFramebuffer, viewportState, scissor);
		}
	}
}

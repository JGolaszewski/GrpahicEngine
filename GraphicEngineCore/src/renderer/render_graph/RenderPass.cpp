#include "renderer/render_graph/RenderPass.h"
#include "renderer/render_graph/FrameBlackboard.h"
#include "renderer/render_graph/RenderView.h"
#include "SceneRenderer.h"
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
		desc.isShaderResource = true;
		desc.mipLevels = 1;

		m_ShadowMapID = builder.CreateAndWriteTexture(desc);
		blackboard.SetShadowMap(m_ShadowMapID);
	}

	bool ShadowPass::Execute(nvrhi::ICommandList* cmd, const Renderer::RenderContext& ctx) {
		const auto& shadowViews = ctx.GetBlackboard().GetShadowViews();
		if (shadowViews.empty()) return true;
		nvrhi::BufferHandle perObjectBuffer = ctx.GetBlackboard().GetPerObjectBuffer();
		nvrhi::BindingSetHandle perObjectBindings = ctx.GetBlackboard().GetPerObjectBindingSet();
		if (!perObjectBuffer || !perObjectBindings) return false;

		nvrhi::TextureHandle physicalShadowMap = ctx.GetPhysicalTexture(m_ShadowMapID);
		if (!physicalShadowMap) return false;

		const nvrhi::TextureDesc& shadowTexDesc = physicalShadowMap->getDesc();

		nvrhi::Viewport shadowViewport = nvrhi::Viewport(float(shadowTexDesc.width), float(shadowTexDesc.height));
		nvrhi::Rect shadowScissor = nvrhi::Rect(shadowTexDesc.width, shadowTexDesc.height);

		if (!m_ShadowMapTexture || m_ShadowMapTexture.Get() != physicalShadowMap.Get()) {
			m_ShadowMapTexture = physicalShadowMap;
			m_ShadowFramebuffers.clear();
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
			if (!shadowFb) return false;

			nvrhi::TextureSubresourceSet subresources;
			subresources.baseArraySlice = shadowSlot;
			subresources.numArraySlices = 1;
			subresources.baseMipLevel = 0;
			subresources.numMipLevels = 1;
			cmd->clearDepthStencilTexture(physicalShadowMap, subresources, true, 1.0f, false, 0);

			for (const auto& packet : view->Queues.Opaque) {
				if (!packet.Pipeline || !packet.VertexBuffer ||
					(packet.ShadowAlphaTest && !packet.MaterialBindings)) return false;
				cmd->writeBuffer(perObjectBuffer, &packet.PerObjectData, sizeof(packet.PerObjectData));
				ctx.GetFrameContext().GetDiagnostics().UploadBytes += sizeof(packet.PerObjectData);
				++ctx.GetFrameContext().GetDiagnostics().DrawCalls;
				nvrhi::GraphicsState state;
				state.pipeline = packet.Pipeline;
				state.framebuffer = shadowFb;
				state.viewport.addViewport(shadowViewport);
				state.viewport.addScissorRect(shadowScissor);

				state.bindings = { perObjectBindings };
				if (packet.ShadowAlphaTest) state.bindings.push_back(packet.MaterialBindings);

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
		return true;
	}

	// ==============================================================================
	// FORWARD PASS
	// ==============================================================================

	ForwardPass::ForwardPass(Engine::SceneRenderer* sceneRenderer)
		: m_SceneRenderer(sceneRenderer) {
	}

	void ForwardPass::Setup(Renderer::RGBuilder& builder, FrameBlackboard& blackboard) {
		m_ShadowMapInputID = blackboard.GetShadowMap();
		builder.Read(m_ShadowMapInputID);
	}

	bool ForwardPass::Execute(nvrhi::ICommandList* cmd, const Renderer::RenderContext& ctx) {
		const auto& sceneViews = ctx.GetBlackboard().GetSceneViews();
		if (sceneViews.empty()) return true;

		nvrhi::TextureHandle physicalShadowMap = ctx.GetPhysicalTexture(m_ShadowMapInputID);
		nvrhi::BufferHandle perObjectBuffer = ctx.GetBlackboard().GetPerObjectBuffer();
		nvrhi::BindingSetHandle perObjectBindings = ctx.GetBlackboard().GetPerObjectBindingSet();
		nvrhi::SamplerHandle shadowSampler = ctx.GetBlackboard().GetShadowSampler();
		nvrhi::BindingLayoutHandle sceneBindingLayout = ctx.GetBlackboard().GetSceneBindingLayout();
		if (!physicalShadowMap || !perObjectBuffer || !perObjectBindings || !shadowSampler || !sceneBindingLayout) return false;

		auto drawQueue = [&](const std::vector<Renderer::RenderPacket>& packets,
			nvrhi::BindingSetHandle sceneBindings,
			nvrhi::IFramebuffer* fb,
			const nvrhi::Viewport& vp,
			const nvrhi::Rect& scissor) {
			for (const auto& packet : packets) {
				if (!packet.Pipeline || !packet.VertexBuffer || !sceneBindings) return false;
				cmd->writeBuffer(perObjectBuffer, &packet.PerObjectData, sizeof(packet.PerObjectData));
				ctx.GetFrameContext().GetDiagnostics().UploadBytes += sizeof(packet.PerObjectData);
				nvrhi::GraphicsState state;
				state.pipeline = packet.Pipeline;
				state.framebuffer = fb;
				state.viewport.addViewport(vp);
				state.viewport.addScissorRect(scissor);

				state.bindings = { perObjectBindings, sceneBindings, packet.MaterialBindings };

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
				++ctx.GetFrameContext().GetDiagnostics().DrawCalls;
			}
			return true;
			};

		for (size_t viewIndex = 0; viewIndex < sceneViews.size(); ++viewIndex) {
			Renderer::RenderView* view = sceneViews[viewIndex];
			if (!view || !view->TargetFramebuffer || !view->CameraBuffer || !view->LightBuffer) return false;
			Renderer::LightsCBData lightData{};
			const uint32_t lightCount = static_cast<uint32_t>(std::min(view->Lights.size(),
				static_cast<size_t>(ENGINE_MAX_LIGHTS)));
			for (uint32_t i = 0; i < lightCount; ++i) lightData.Lights[i] = view->Lights[i];
			lightData.LightCount = lightCount;
			cmd->writeBuffer(view->LightBuffer, &lightData, sizeof(lightData));
			ctx.GetFrameContext().GetDiagnostics().UploadBytes += sizeof(lightData);
			nvrhi::BindingSetHandle sceneBindings = m_SceneRenderer
				? m_SceneRenderer->GetSceneBindingSet(viewIndex, view->LightBuffer, physicalShadowMap,
					shadowSampler, view->CameraBuffer, ctx.GetFrameContext().GetDiagnostics())
				: nullptr;
			if (!sceneBindings) return false;

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

			if (!drawQueue(view->Queues.Opaque, sceneBindings, mainFramebuffer, viewportState, scissor) ||
				!drawQueue(view->Queues.Masked, sceneBindings, mainFramebuffer, viewportState, scissor) ||
				!drawQueue(view->Queues.Skybox, sceneBindings, mainFramebuffer, viewportState, scissor) ||
				!drawQueue(view->Queues.Transparent, sceneBindings, mainFramebuffer, viewportState, scissor) ||
				!drawQueue(view->Queues.UI, sceneBindings, mainFramebuffer, viewportState, scissor)) return false;
		}
		return true;
	}

	// ==============================================================================
	// DEBUG PASS
	// ==============================================================================

	DebugPass::DebugPass(Engine::SceneRenderer* sceneRenderer)
		: m_SceneRenderer(sceneRenderer) {
	}

	void DebugPass::Setup(Renderer::RGBuilder&, FrameBlackboard&) {
	}

	bool DebugPass::Execute(nvrhi::ICommandList* cmd, const Renderer::RenderContext& ctx) {
		if (!m_SceneRenderer) {
			return false;
		}

		for (const Renderer::RenderView* view : ctx.GetBlackboard().GetSceneViews()) {
			if (view) {
				m_SceneRenderer->RenderDebugGeometry(cmd, *view);
			}
		}
		return true;
	}
}

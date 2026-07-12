#include "RenderPass.h"

namespace Renderer {
	ShadowPass::ShadowPass(std::vector<ShadowDrawData> shadowData, PipelineCache* pipelineCache)
		: m_ShadowData(std::move(shadowData)), m_PipelineCache(pipelineCache) {} 

	void ShadowPass::Setup(Renderer::RGBuilder& builder) override {
		Renderer::RGTextureDesc desc{};
		desc.width = Engine::SceneRenderer::ShadowMapSize;
		desc.height = Engine::SceneRenderer::ShadowMapSize;
		// TODO: add other properties to desc as needed, such as format, array size, etc.

		m_ShadowMapID = builder.CreateTexture(desc);
		builder.Write(m_ShadowMapID);
	}

	void ShadowPass::Execute(nvrhi::ICommandList* cmd, const Renderer::RenderContext& ctx) {
		nvrhi::TextureHandle physicalShadowMap = ctx.GetPhysicalTexture(m_ShadowMapID);
		const nvrhi::TextureDesc& shadowTexDesc = physicalShadowMap->getDesc();

		nvrhi::Viewport shadowViewport = nvrhi::Viewport(float(shadowTexDesc.width), float(shadowTexDesc.height));
		nvrhi::Rect shadowScissor = nvrhi::Rect(shadowTexDesc.width, shadowTexDesc.height);

		nvrhi::BindingSetHandle passBindingSet = GetOrCreatePerObjectBindingSet(physicalShadowMap);

		for (uint32_t shadowSlot = 0; shadowSlot < m_ShadowData.size(); ++shadowSlot) {
			const auto& shadowDraw = m_ShadowData[shadowSlot];

			DirectX::XMMATRIX lightViewProj = DirectX::XMLoadFloat4x4(&shadowDraw.Light.LightViewProj);

			nvrhi::IFramebuffer* shadowFb = GetOrCreateShadowFramebuffer(physicalShadowMap, shadowSlot);

			// 4. Czyszczenie tekstury głębi dla TEJ warstwy
			nvrhi::TextureSubresourceSet subresources;
			subresources.baseArraySlice = shadowSlot;
			subresources.numArraySlices = 1;
			subresources.baseMipLevel = 0;
			subresources.numMipLevels = 1;
			cmd->clearDepthStencilTexture(physicalShadowMap, subresources, true, 1.0f, false, 0);

			// 5. Rysowanie obiektów z przygotowanych pakietów (Render Packets)
			for (const auto& packet : shadowDraw.Packets) {
				// UWAGA: Nie musimy już sprawdzać 'if (!entity->CastShadows)'.
				// Ekstraktor zrobił to za nas! Tutaj wpadają TYLKO właściwe obiekty.

				// Obliczenie macierzy MVP
				DirectX::XMMATRIX model = DirectX::XMLoadFloat4x4(&packet.WorldMatrix);
				DirectX::XMMATRIX lightMvp = model * lightViewProj;

				// Zapisanie danych per-obiekt
				PerObjectData perObjectData{};
				DirectX::XMStoreFloat4x4(&perObjectData.MVP, DirectX::XMMatrixTranspose(lightMvp));
				DirectX::XMStoreFloat4x4(&perObjectData.World, DirectX::XMMatrixTranspose(model));
				perObjectData.ObjectParams = {
					packet.ReceiveShadows ? 1.0f : 0.0f,
					packet.ForceWireframe ? 1.0f : 0.0f,
					0.0f, 0.0f
				};

				cmd->writeBuffer(m_PerObjectBuffer, &perObjectData, sizeof(PerObjectData));

				// Pobranie klucza Pipeline'u z pakietu i nadpisanie ustawień dla Shadow Pass
				Engine::PipelineKey key = packet.PipelineStateKey;
				key.PS = nullptr; // Nie potrzebujemy Pixel Shadera dla cieni
				key.Wireframe = false;
				key.DepthTest = true;
				key.DepthWrite = true;

				nvrhi::GraphicsState state;
				state.pipeline = m_PipelineCache->GetOrCreatePipeline(key, shadowFb);
				state.framebuffer = shadowFb;
				state.viewport.addViewport(shadowViewport);
				state.viewport.addScissorRect(shadowScissor);

				// Powiązanie buforów stałych (Globalny + Z konkretnego materiału w pakiecie)
				state.bindings = { passBindingSet, packet.MaterialBindings };

				// Bindowanie buforów wierzchołków/indeksów
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

				// Aplikacja stanu i wywołanie Draw Call
				cmd->setGraphicsState(state);

				nvrhi::DrawArguments args;
				args.instanceCount = 1;
				if (packet.IndexBuffer) {
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


	ForwardPass::ForwardPass(Engine::SceneRenderer* renderer, Renderer::RGResourceID shadowMap, Renderer::Viewport* vp)
		: m_SceneRenderer(renderer), m_ShadowMapInputID(shadowMap), m_Viewport(vp) {
	}

	void ForwardPass::Setup(Renderer::RGBuilder& builder) override {
		builder.Read(m_ShadowMapInputID);
	}

	void ForwardPass::Execute(nvrhi::ICommandList* cmd, const Renderer::RenderContext& ctx) override {
		nvrhi::TextureHandle physicalShadowMap = ctx.GetPhysicalTexture(m_ShadowMapInputID);
		nvrhi::IFramebuffer* mainFramebuffer = m_Viewport->GetFramebuffer();
		DirectX::XMMATRIX viewProj = m_Viewport->GetCamera()->GetViewProjection();

		m_SceneRenderer->DrawForward(cmd, mainFramebuffer, viewProj, physicalShadowMap);
	}
}
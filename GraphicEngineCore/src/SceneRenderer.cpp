#include "SceneRenderer.h"

#include <algorithm>
#include <cmath>

namespace {
	struct PerObjectData {
		DirectX::XMFLOAT4X4 MVP;
		DirectX::XMFLOAT4X4 World;
		DirectX::XMFLOAT4 ObjectParams;
	};

	struct LightsCBData {
		Renderer::DirectionalLightData Lights[Engine::SceneRenderer::MaxLights];
		uint32_t LightCount = 0;
		float Padding[3] = { 0.0f, 0.0f, 0.0f };
	};

	DirectX::XMMATRIX ComputeSpotLightViewProj(const Renderer::DirectionalLightData& lightData) {
		using namespace DirectX;

		XMFLOAT3 position = XMFLOAT3(lightData.PositionRange.x, lightData.PositionRange.y, lightData.PositionRange.z);
		XMFLOAT3 direction = XMFLOAT3(lightData.DirectionIntensity.x, lightData.DirectionIntensity.y, lightData.DirectionIntensity.z);

		XMVECTOR dir = XMVector3Normalize(XMLoadFloat3(&direction));
		if(XMVectorGetX(XMVector3LengthSq(dir)) < 1e-6f) {
			dir = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
		}

		XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		const float upDot = std::abs(XMVectorGetX(XMVector3Dot(up, dir)));
		if(upDot > 0.98f) {
			up = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
		}

		XMVECTOR pos = XMLoadFloat3(&position);
		XMMATRIX view = XMMatrixLookToLH(pos, dir, up);

		const float outerCos = std::clamp(lightData.ConeParams.y, -1.0f, 1.0f);
		const float outerAngle = std::acos(outerCos);
		const float fovY = std::clamp(outerAngle * 2.0f, 0.2f, DirectX::XM_PI - 0.01f);
		const float nearPlane = 0.05f;
		const float farPlane = std::max(lightData.PositionRange.w, nearPlane + 0.01f);

		XMMATRIX proj = XMMatrixPerspectiveFovLH(fovY, 1.0f, nearPlane, farPlane);
		return view * proj;
	}
}


nvrhi::BindingSetHandle Engine::SceneRenderer::GetOrCreatePerObjectBindingSet(nvrhi::TextureHandle physicalShadowMap) {
	nvrhi::ITexture* rawTexturePtr = physicalShadowMap.Get();
	auto it = m_PerObjectBindingSetsCache.find(rawTexturePtr);

	if (it != m_PerObjectBindingSetsCache.end()) {
		return it->second;
	}

	nvrhi::BindingSetDesc setDesc;
	setDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_PerObjectBuffer));
	setDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(2, m_LightBuffer));
	setDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(3, physicalShadowMap));
	setDesc.addItem(nvrhi::BindingSetItem::Sampler(3, m_ShadowSampler));

	nvrhi::BindingSetHandle newBindingSet = m_Device->createBindingSet(setDesc, m_PerObjectBindingLayout);

	m_PerObjectBindingSetsCache[rawTexturePtr] = newBindingSet;

	return newBindingSet;
}

void Engine::SceneRenderer::Init(nvrhi::IDevice* device, MaterialSystem materialSystem) {
	m_PipelineCache.Init(device);

	m_Device = device;

	nvrhi::BufferDesc cbDesc;
    cbDesc.byteSize = sizeof(PerObjectData);
	cbDesc.isConstantBuffer = true;
	cbDesc.isVolatile = true;
	cbDesc.debugName = "PerObject_MVP_Buffer";
	m_PerObjectBuffer = device->createBuffer(cbDesc);

	nvrhi::BufferDesc lightCbDesc;
	lightCbDesc.byteSize = sizeof(LightsCBData);
	lightCbDesc.isConstantBuffer = true;
	lightCbDesc.isVolatile = true;
	lightCbDesc.debugName = "Lights_Buffer";
	m_LightBuffer = device->createBuffer(lightCbDesc);

	nvrhi::BindingLayoutDesc layoutDesc;
	layoutDesc.visibility = nvrhi::ShaderType::All;
	layoutDesc.bindings = {
     nvrhi::BindingLayoutItem::ConstantBuffer(0),
     nvrhi::BindingLayoutItem::ConstantBuffer(2),
		nvrhi::BindingLayoutItem::Texture_SRV(3),
		nvrhi::BindingLayoutItem::Sampler(3)
	};
	m_PerObjectBindingLayout = device->createBindingLayout(layoutDesc);

	nvrhi::TextureDesc shadowDesc;
	shadowDesc.width = ShadowMapSize;
	shadowDesc.height = ShadowMapSize;
	shadowDesc.arraySize = MaxShadowedLights;
	shadowDesc.dimension = nvrhi::TextureDimension::Texture2DArray;
	shadowDesc.format = nvrhi::Format::D32;
	shadowDesc.isTypeless = true;
	shadowDesc.isRenderTarget = true;
	shadowDesc.isShaderResource = true;
	shadowDesc.initialState = nvrhi::ResourceStates::DepthWrite;
	shadowDesc.keepInitialState = true;
	shadowDesc.debugName = "ShadowMapArray";
	m_ShadowMapArray = device->createTexture(shadowDesc);

	m_ShadowFramebuffers.clear();
	m_ShadowFramebuffers.reserve(MaxShadowedLights);
	for(uint32_t i = 0; i < MaxShadowedLights; ++i) {
		nvrhi::FramebufferAttachment depthAttachment;
		depthAttachment.setTexture(m_ShadowMapArray).setArraySlice(i);

		nvrhi::FramebufferDesc shadowFbDesc;
		shadowFbDesc.setDepthAttachment(depthAttachment);
		m_ShadowFramebuffers.push_back(device->createFramebuffer(shadowFbDesc));
	}

	nvrhi::SamplerDesc shadowSamplerDesc;
	shadowSamplerDesc.setAddressU(nvrhi::SamplerAddressMode::Border)
		.setAddressV(nvrhi::SamplerAddressMode::Border)
		.setAddressW(nvrhi::SamplerAddressMode::Border)
		.setMinFilter(true)
		.setMagFilter(true)
		.setMipFilter(false)
		.setReductionType(nvrhi::SamplerReductionType::Comparison)
		.setBorderColor(nvrhi::Color(1.0f, 1.0f, 1.0f, 1.0f));
	m_ShadowSampler = device->createSampler(shadowSamplerDesc);

	nvrhi::BindingSetDesc setDesc;
	setDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_PerObjectBuffer));
    setDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(2, m_LightBuffer));
    setDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(3, m_ShadowMapArray));
	setDesc.addItem(nvrhi::BindingSetItem::Sampler(3, m_ShadowSampler));
	m_PerObjectBindingSet = device->createBindingSet(setDesc, m_PerObjectBindingLayout);
}

void Engine::SceneRenderer::Submit(Renderer::Entity* entity) {
	if(entity && entity->mesh && entity->material) {
		m_CommandQueue.push_back(entity);
	}
}

void Engine::SceneRenderer::AddLight(Renderer::SpotLight* light) {
	if(m_Lights.size() >= MaxLights) {
		return;
	}

	m_Lights.push_back(light->GetDirectionalLightData());
}

void Engine::SceneRenderer::ClearLights() {
	m_Lights.clear();
}

void Engine::SceneRenderer::DrawShadows(nvrhi::ICommandList* cmd, nvrhi::TextureHandle physicalShadowMap) {
	// Reset light shadow indices to -1, indicating no shadows are cast by default.
	for (auto& light : m_Lights) {
		light.ShadowParams.x = -1.0f; 
	}

	// Filter lights that use position and cast shadows, storing their indices in shadowedLightIndices.
	std::vector<uint32_t> shadowedLightIndices;
	shadowedLightIndices.reserve(MaxShadowedLights); 
	for (uint32_t i = 0; i < m_Lights.size() && shadowedLightIndices.size() < MaxShadowedLights; ++i) {
		const bool usePosition = m_Lights[i].ColorUsePosition.w > 0.5f;
		const bool castShadows = m_Lights[i].ShadowParams.w > 0.5f;
		if (usePosition && castShadows) {
			shadowedLightIndices.push_back(i);
		}
	}

	// Retrieve information about the dynamic shadow texture
	const nvrhi::TextureDesc& shadowTexDesc = physicalShadowMap->getDesc();
	nvrhi::Viewport shadowViewport = nvrhi::Viewport(float(shadowTexDesc.width), float(shadowTexDesc.height)); 
	nvrhi::Rect shadowScissor = nvrhi::Rect(shadowTexDesc.width, shadowTexDesc.height); 

	// Render shadow maps for each light that casts shadows, using the shadowedLightIndices vector to access the relevant lights.
	for (uint32_t shadowSlot = 0; shadowSlot < shadowedLightIndices.size(); ++shadowSlot) {
		const uint32_t lightIndex = shadowedLightIndices[shadowSlot]; 
		Renderer::DirectionalLightData& lightData = m_Lights[lightIndex]; 

		const DirectX::XMMATRIX lightViewProj = ComputeSpotLightViewProj(lightData); 
		DirectX::XMStoreFloat4x4(&lightData.LightViewProj, DirectX::XMMatrixTranspose(lightViewProj)); 
		lightData.ShadowParams.x = static_cast<float>(shadowSlot); 

		// UWAGA: Zamiast sztywnego m_ShadowFramebuffers[shadowSlot], pobieramy Framebuffer z cache
		nvrhi::IFramebuffer* shadowFb = GetOrCreatePerObjectBindingSet(physicalShadowMap, shadowSlot);

		// Clear the depth-stencil texture for the current shadow slot, preparing it for rendering the shadow map.
		nvrhi::TextureSubresourceSet subresources;
		subresources.baseArraySlice = shadowSlot; 
		subresources.numArraySlices = 1; 
		subresources.baseMipLevel = 0; 
		subresources.numMipLevels = 1; 
		cmd->clearDepthStencilTexture(physicalShadowMap, subresources, true, 1.0f, false, 0); 

		// Draw geometry for each entity in the command queue, applying the shadow map rendering logic.
		for (auto* entity : m_CommandQueue) {
			if (!entity->CastShadows || entity->DisableDepthTest) {
				continue; 
			}

			DirectX::XMMATRIX model = entity->transform.GetWorldMat(); 
			DirectX::XMMATRIX lightMvp = model * lightViewProj; 

			PerObjectData perObjectData{};
			DirectX::XMStoreFloat4x4(&perObjectData.MVP, DirectX::XMMatrixTranspose(lightMvp)); 
			DirectX::XMStoreFloat4x4(&perObjectData.World, DirectX::XMMatrixTranspose(model)); 
			perObjectData.ObjectParams = {
				entity->ReceiveShadows ? 1.0f : 0.0f,
				entity->ForceWireframe ? 1.0f : 0.0f,
				0.0f, 0.0f
			};

			cmd->writeBuffer(m_PerObjectBuffer, &perObjectData, sizeof(PerObjectData)); 

			Engine::PipelineKey key;
			key.VS = m_MaterialSystem->GetStandardVS();
			key.PS = nullptr;
			key.GlobalLayout = m_PerObjectBindingLayout; 
			key.MaterialLayout = m_MaterialSystem->GetStandardMaterialLayout();
			key.Wireframe = false; 
			key.DepthTest = true; 
			key.DepthWrite = true; 
			key.CullMode = entity->CullMode; 

			nvrhi::GraphicsState state;
			state.pipeline = m_PipelineCache.GetOrCreatePipeline(key, shadowFb); 
			state.framebuffer = shadowFb; 
			state.viewport.addViewport(shadowViewport); 
			state.viewport.addScissorRect(shadowScissor); 

			// UWAGA: Używamy dynamicznego BindingSet
			state.bindings = {
				GetOrCreatePerObjectBindingSet(physicalShadowMap),
				entity->material->GetBindingSet()
			};

			nvrhi::VertexBufferBinding vbb;
			vbb.buffer = entity->mesh->GetVertexBuffer();
			vbb.slot = 0;
			vbb.offset = 0;
			state.vertexBuffers = { vbb };

			if (entity->mesh->HasIndices()) {
				state.indexBuffer.buffer = entity->mesh->GetIndexBuffer();
				state.indexBuffer.format = nvrhi::Format::R32_UINT;
				state.indexBuffer.offset = 0;
			}

			cmd->setGraphicsState(state);

			nvrhi::DrawArguments args;
			args.instanceCount = 1;
			if (entity->mesh->HasIndices()) {
				args.vertexCount = entity->mesh->GetIndexCount();
				cmd->drawIndexed(args);
			}
			else {
				args.vertexCount = entity->mesh->GetVertexCount();
				cmd->draw(args);
			}
		}
	}
}

void Engine::SceneRenderer::DrawForward(nvrhi::ICommandList* cmd, nvrhi::IFramebuffer* fb, const DirectX::XMMATRIX& viewProj, nvrhi::TextureHandle physicalShadowMap) {
	const nvrhi::FramebufferInfoEx& fbInfo = fb->getFramebufferInfo(); 
	nvrhi::Viewport viewport = nvrhi::Viewport(float(fbInfo.width), float(fbInfo.height)); 
	nvrhi::Rect scissor = nvrhi::Rect(fbInfo.width, fbInfo.height); 

	LightsCBData lightData{};
	const uint32_t lightCount = static_cast<uint32_t>(std::min(m_Lights.size(), static_cast<size_t>(MaxLights))); 
	for (uint32_t i = 0; i < lightCount; ++i) {
		lightData.Lights[i] = m_Lights[i];
	}
	lightData.LightCount = lightCount; 

	cmd->writeBuffer(m_LightBuffer, &lightData, sizeof(LightsCBData)); 

	for (auto* entity : m_CommandQueue) {
		DirectX::XMMATRIX model = entity->transform.GetWorldMat(); 
		DirectX::XMMATRIX mvp = model * viewProj; 

		PerObjectData perObjectData{};
		DirectX::XMStoreFloat4x4(&perObjectData.MVP, DirectX::XMMatrixTranspose(mvp)); 
		DirectX::XMStoreFloat4x4(&perObjectData.World, DirectX::XMMatrixTranspose(model)); 
		perObjectData.ObjectParams = {
			entity->ReceiveShadows ? 1.0f : 0.0f,
			entity->ForceWireframe ? 1.0f : 0.0f,
			0.0f, 0.0f
		};

		cmd->writeBuffer(m_PerObjectBuffer, &perObjectData, sizeof(PerObjectData)); 

		Engine::PipelineKey key;
		key.VS = m_MaterialSystem->GetStandardVS();
		key.PS = m_MaterialSystem->GetStandardPS();
		key.GlobalLayout = m_PerObjectBindingLayout;
		key.MaterialLayout = m_MaterialSystem->GetStandardMaterialLayout();
		key.Wireframe = entity->ForceWireframe;
		key.DepthTest = !entity->DisableDepthTest;
		key.DepthWrite = !entity->DisableDepthTest;
		key.CullMode = entity->ForceWireframe ? nvrhi::RasterCullMode::None : entity->CullMode;

		nvrhi::GraphicsState state;
		state.pipeline = m_PipelineCache.GetOrCreatePipeline(key, fb); 
		state.framebuffer = fb; 
		state.viewport.addViewport(viewport); 
		state.viewport.addScissorRect(scissor); 

		state.bindings = {
			GetOrCreatePerObjectBindingSet(physicalShadowMap),
			entity->material->GetBindingSet()
		};

		nvrhi::VertexBufferBinding vbb;
		vbb.buffer = entity->mesh->GetVertexBuffer();
		vbb.slot = 0; 
		vbb.offset = 0; 
		state.vertexBuffers = { vbb }; 

		if (entity->mesh->HasIndices()) {
			state.indexBuffer.buffer = entity->mesh->GetIndexBuffer(); 
			state.indexBuffer.format = nvrhi::Format::R32_UINT;
			state.indexBuffer.offset = 0;
		}

		cmd->setGraphicsState(state); 

		nvrhi::DrawArguments args;
		args.instanceCount = 1; 

		if (entity->mesh->HasIndices()) {
			args.vertexCount = entity->mesh->GetIndexCount();
			cmd->drawIndexed(args);
		}
		else {
			args.vertexCount = entity->mesh->GetVertexCount();
			cmd->draw(args);
		}
	}
}


void Engine::SceneRenderer::RenderAll(nvrhi::ICommandList* cmd, nvrhi::IFramebuffer* fb, const DirectX::XMMATRIX& viewProj) {
   for(auto& light : m_Lights) {
		light.ShadowParams.x = -1.0f;
	}

  std::vector<uint32_t> shadowedLightIndices;
	shadowedLightIndices.reserve(MaxShadowedLights);
	for(uint32_t i = 0; i < m_Lights.size() && shadowedLightIndices.size() < MaxShadowedLights; ++i) {
		const bool usePosition = m_Lights[i].ColorUsePosition.w > 0.5f;
		const bool castShadows = m_Lights[i].ShadowParams.w > 0.5f;
		if(usePosition && castShadows) {
			shadowedLightIndices.push_back(i);
		}
	}

	for(uint32_t shadowSlot = 0; shadowSlot < shadowedLightIndices.size(); ++shadowSlot) {
		const uint32_t lightIndex = shadowedLightIndices[shadowSlot];
		Renderer::DirectionalLightData& lightData = m_Lights[lightIndex];

		const DirectX::XMMATRIX lightViewProj = ComputeSpotLightViewProj(lightData);
		DirectX::XMStoreFloat4x4(&lightData.LightViewProj, DirectX::XMMatrixTranspose(lightViewProj));
		lightData.ShadowParams.x = static_cast<float>(shadowSlot);

		nvrhi::IFramebuffer* shadowFb = m_ShadowFramebuffers[shadowSlot];
		const nvrhi::FramebufferInfoEx& shadowFbInfo = shadowFb->getFramebufferInfo();

		nvrhi::TextureSubresourceSet subresources;
		subresources.baseArraySlice = shadowSlot;
		subresources.numArraySlices = 1;
		subresources.baseMipLevel = 0;
		subresources.numMipLevels = 1;
		cmd->clearDepthStencilTexture(m_ShadowMapArray, subresources, true, 1.0f, false, 0);

		nvrhi::Viewport shadowViewport = nvrhi::Viewport(float(shadowFbInfo.width), float(shadowFbInfo.height));
		nvrhi::Rect shadowScissor = nvrhi::Rect(shadowFbInfo.width, shadowFbInfo.height);

		for(auto* entity : m_CommandQueue) {ad
			if(!entity->CastShadows || entity->DisableDepthTest) {
				continue;
			}

			DirectX::XMMATRIX model = entity->transform.GetWorldMat();
			DirectX::XMMATRIX lightMvp = model * lightViewProj;

			PerObjectData perObjectData{};
			DirectX::XMStoreFloat4x4(&perObjectData.MVP, DirectX::XMMatrixTranspose(lightMvp));
			DirectX::XMStoreFloat4x4(&perObjectData.World, DirectX::XMMatrixTranspose(model));
            perObjectData.ObjectParams = {
				entity->ReceiveShadows ? 1.0f : 0.0f,
				entity->ForceWireframe ? 1.0f : 0.0f,
				0.0f,
				0.0f
			};

			cmd->writeBuffer(m_PerObjectBuffer, &perObjectData, sizeof(PerObjectData));

			Engine::PipelineKey key;
			key.VS = entity->material->GetVS();
			key.PS = nullptr;
			key.GlobalLayout = m_PerObjectBindingLayout;
			key.MaterialLayout = entity->material->GetBindingLayout();
			key.Wireframe = false;
			key.DepthTest = true;
			key.DepthWrite = true;
			key.CullMode = entity->CullMode;

			nvrhi::GraphicsState state;
			state.pipeline = m_PipelineCache.GetOrCreatePipeline(key, shadowFb);
			state.framebuffer = shadowFb;
			state.viewport.addViewport(shadowViewport);
			state.viewport.addScissorRect(shadowScissor);
			state.bindings = {
				m_PerObjectBindingSet,
				entity->material->GetBindingSet()
			};

			nvrhi::VertexBufferBinding vbb;
			vbb.buffer = entity->mesh->GetVertexBuffer();
			vbb.slot = 0;
			vbb.offset = 0;
			state.vertexBuffers = { vbb };

			if(entity->mesh->HasIndices()) {
				state.indexBuffer.buffer = entity->mesh->GetIndexBuffer();
				state.indexBuffer.format = nvrhi::Format::R32_UINT;
				state.indexBuffer.offset = 0;
			}

			cmd->setGraphicsState(state);

			nvrhi::DrawArguments args;
			args.instanceCount = 1;
			if(entity->mesh->HasIndices()) {
				args.vertexCount = entity->mesh->GetIndexCount();
				cmd->drawIndexed(args);
			} else {
				args.vertexCount = entity->mesh->GetVertexCount();
				cmd->draw(args);
			}
		}
	}

	const nvrhi::FramebufferInfoEx& fbInfo = fb->getFramebufferInfo();
	nvrhi::Viewport viewport = nvrhi::Viewport(float(fbInfo.width), float(fbInfo.height));
	nvrhi::Rect scissor = nvrhi::Rect(fbInfo.width, fbInfo.height);

   LightsCBData lightData{};
	const uint32_t lightCount = static_cast<uint32_t>(std::min(m_Lights.size(), static_cast<size_t>(MaxLights)));
	for(uint32_t i = 0; i < lightCount; ++i) {
		lightData.Lights[i] = m_Lights[i];
	}
	lightData.LightCount = lightCount;

	cmd->writeBuffer(m_LightBuffer, &lightData, sizeof(LightsCBData));

	for(auto* entity : m_CommandQueue) {
		DirectX::XMMATRIX model = entity->transform.GetWorldMat();
		DirectX::XMMATRIX mvp = model * viewProj;
      PerObjectData perObjectData{};
        DirectX::XMStoreFloat4x4(&perObjectData.MVP, DirectX::XMMatrixTranspose(mvp));
		DirectX::XMStoreFloat4x4(&perObjectData.World, DirectX::XMMatrixTranspose(model));
        perObjectData.ObjectParams = {
			entity->ReceiveShadows ? 1.0f : 0.0f,
			entity->ForceWireframe ? 1.0f : 0.0f,
			0.0f,
			0.0f
		};

     cmd->writeBuffer(m_PerObjectBuffer, &perObjectData, sizeof(PerObjectData));

		Engine::PipelineKey key;
		key.VS = entity->material->GetVS();
		key.PS = entity->material->GetPS();
		key.GlobalLayout = m_PerObjectBindingLayout;
		key.MaterialLayout = entity->material->GetBindingLayout();
		key.Wireframe = entity->ForceWireframe;
		key.DepthTest = !entity->DisableDepthTest;
		key.DepthWrite = !entity->DisableDepthTest;
        key.CullMode = entity->ForceWireframe ? nvrhi::RasterCullMode::None : entity->CullMode;

		nvrhi::GraphicsState state;

		state.pipeline = m_PipelineCache.GetOrCreatePipeline(key, fb);


		state.framebuffer = fb;

		state.viewport.addViewport(viewport);
		state.viewport.addScissorRect(scissor);

		state.bindings = {
			m_PerObjectBindingSet,
			entity->material->GetBindingSet()
		};

		nvrhi::VertexBufferBinding vbb;
		vbb.buffer = entity->mesh->GetVertexBuffer();
		vbb.slot = 0;
		vbb.offset = 0;
		state.vertexBuffers = { vbb };

		if(entity->mesh->HasIndices()) {
			state.indexBuffer.buffer = entity->mesh->GetIndexBuffer();
			state.indexBuffer.format = nvrhi::Format::R32_UINT;
			state.indexBuffer.offset = 0;
		}

		cmd->setGraphicsState(state);

		nvrhi::DrawArguments args;
		args.instanceCount = 1;

		if(entity->mesh->HasIndices()) {
			args.vertexCount = entity->mesh->GetIndexCount();
			cmd->drawIndexed(args);
		} else {
			args.vertexCount = entity->mesh->GetVertexCount();
			cmd->draw(args);
		}
	}

	m_CommandQueue.clear();
}

nvrhi::BindingLayoutHandle Engine::SceneRenderer::GetPerObjectBindingLayout() const {
	return m_PerObjectBindingLayout;
}

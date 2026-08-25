#include "SceneRenderer.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "renderer/Entity.h"
#include "renderer/Mesh.h"
#include "renderer/Material.h"
#include "Scene.h"

namespace {
	struct PerObjectData {
		DirectX::XMFLOAT4X4 MVP;
		DirectX::XMFLOAT4X4 World;
		DirectX::XMFLOAT4 ObjectParams;
		uint8_t Padding[112]{};
	};

	static_assert(sizeof(PerObjectData) == Engine::SceneRenderer::PerObjectDataStride);

	DirectX::XMMATRIX ComputeSpotLightViewProj(const Renderer::DirectionalLightData& lightData) {
		using namespace DirectX;

		XMFLOAT3 position = XMFLOAT3(lightData.PositionRange.x, lightData.PositionRange.y, lightData.PositionRange.z);
		XMFLOAT3 direction = XMFLOAT3(lightData.DirectionIntensity.x, lightData.DirectionIntensity.y, lightData.DirectionIntensity.z);

		XMVECTOR dir = XMVector3Normalize(XMLoadFloat3(&direction));
		if (XMVectorGetX(XMVector3LengthSq(dir)) < 1e-6f) {
			dir = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
		}

		XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		const float upDot = std::abs(XMVectorGetX(XMVector3Dot(up, dir)));
		if (upDot > 0.98f) {
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

// --- Initialization ---

Engine::SceneRenderer::SceneRenderer(nvrhi::IDevice* device) {}

void Engine::SceneRenderer::Init(nvrhi::IDevice* device, Renderer::MaterialSystem* materialSystem) {
	m_MaterialSystem = materialSystem;
	m_PipelineCache.Init(device);

	m_Device = device;

	nvrhi::BufferDesc lightCbDesc;
	lightCbDesc.byteSize = sizeof(Renderer::LightsCBData);
	lightCbDesc.isConstantBuffer = true;
	lightCbDesc.isVolatile = true;
	lightCbDesc.debugName = "Lights_Buffer";
	lightCbDesc.maxVersions = 3;
	m_LightBuffer = device->createBuffer(lightCbDesc);

	nvrhi::BindingLayoutDesc perObjectLayoutDesc;
	perObjectLayoutDesc.visibility = nvrhi::ShaderType::All;
	perObjectLayoutDesc.bindings = {
		nvrhi::BindingLayoutItem::ConstantBuffer(0)
	};
	m_PerObjectBindingLayout = device->createBindingLayout(perObjectLayoutDesc);

	nvrhi::BindingLayoutDesc sceneLayoutDesc;
	sceneLayoutDesc.visibility = nvrhi::ShaderType::All;
	sceneLayoutDesc.bindings = {
		nvrhi::BindingLayoutItem::ConstantBuffer(2),
		nvrhi::BindingLayoutItem::Texture_SRV(3),
		nvrhi::BindingLayoutItem::Sampler(3)
	};
	m_SceneBindingLayout = device->createBindingLayout(sceneLayoutDesc);

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
}


// --- Uploads ---

void Engine::SceneRenderer::CullAndFillView(Renderer::RenderView* view, nvrhi::ICommandList* commandList, uint32_t viewIndex) {
	if (!view || !view->TargetScene) return;
	if (!commandList) return;

	DirectX::XMMATRIX viewProj = view->ViewProjection;
	nvrhi::IFramebuffer* framebuffer = view->TargetFramebuffer; // FIXME 

	nvrhi::FramebufferInfo fbInfo;
	if (framebuffer) {
		fbInfo = framebuffer->getFramebufferInfo();
	}
	else if (view->ViewType == Renderer::RenderView::Type::Shadow) {
		fbInfo.depthFormat = nvrhi::Format::D32;
	}
	else {
		return;
	}

	nvrhi::BufferDesc perObjectDesc;
	perObjectDesc.byteSize = MaxPerObjectDataBytesPerView;
	perObjectDesc.isConstantBuffer = true;
	perObjectDesc.debugName = "PerObjectDataPerView";
	nvrhi::BufferHandle perObjectBuffer = m_Device->createBuffer(perObjectDesc);
	if (!perObjectBuffer) {
		std::cerr << "ERROR: Could not allocate per-object buffer for view " << viewIndex << std::endl;
		return;
	}

	uint32_t perObjectDataOffset = 0;

	for (const auto& entity : view->TargetScene->GetEntities()) {
		if (!entity || !entity->mesh || !entity->material) {
			++m_SubmissionStats.ObjectsCulled;
			continue;
		}

		if (view->ViewType == Renderer::RenderView::Type::Shadow && !entity->CastShadows) {
			++m_SubmissionStats.ObjectsCulled;
			continue;
		}

		if (perObjectDataOffset + PerObjectDataStride > MaxPerObjectDataBytesPerView) {
			std::cerr << "WARN: Per-object budget exhausted for view " << viewIndex
				<< "; skipping remaining object submission." << std::endl;
			++m_SubmissionStats.ObjectsCulled;
			continue;
		}

		const DirectX::XMMATRIX world = entity->transform.GetWorldMat();
		DirectX::XMFLOAT3 worldCenter;
		DirectX::XMStoreFloat3(&worldCenter,
			DirectX::XMVector3TransformCoord(DirectX::XMVectorZero(), world));
		const float maxScale = (std::max)({
			std::abs(entity->transform.Scale.x),
			std::abs(entity->transform.Scale.y),
			std::abs(entity->transform.Scale.z)
		});
		if (!view->IsSphereVisible(worldCenter, maxScale * 1.732f)) {
			++m_SubmissionStats.ObjectsCulled;
			continue;
		}

		PerObjectData perObjectData{};
		DirectX::XMMATRIX mvp = world * viewProj;

		DirectX::XMStoreFloat4x4(&perObjectData.World, DirectX::XMMatrixTranspose(world));
		DirectX::XMStoreFloat4x4(&perObjectData.MVP, DirectX::XMMatrixTranspose(mvp));

		perObjectData.ObjectParams = {
			entity->ReceiveShadows ? 1.0f : 0.0f,
			entity->ForceWireframe ? 1.0f : 0.0f,
			0.0f, 0.0f
		};

		commandList->writeBuffer(perObjectBuffer, &perObjectData, PerObjectDataStride, perObjectDataOffset);

		auto packet = CreateRenderPacket(entity.get(), perObjectBuffer,
			nvrhi::BufferRange(perObjectDataOffset, PerObjectDataStride));
		packet.Queue = Renderer::RenderQueueType::Opaque;
		perObjectDataOffset += PerObjectDataStride;
		++m_SubmissionStats.ObjectsSubmitted;
		m_SubmissionStats.PerObjectBytesUsed += PerObjectDataStride;

		Engine::PipelineKey key;
		key.VS = m_MaterialSystem->GetStandardVS();
		key.PerObjectLayout = m_PerObjectBindingLayout;
		key.SceneLayout = m_SceneBindingLayout;
		key.MaterialLayout = m_MaterialSystem->GetStandardMaterialLayout();
		key.DepthWrite = true;

		if (view->ViewType == Renderer::RenderView::Type::Shadow) {
			key.PS = nullptr;
			key.SceneLayout = nullptr;
			key.MaterialLayout = nullptr;
			key.Wireframe = false;
			key.DepthTest = true;
			key.CullMode = nvrhi::RasterCullMode::Back;
		}
		else {
			key.PS = m_MaterialSystem->GetStandardPS();
			key.Wireframe = entity->ForceWireframe;
			key.DepthTest = !entity->DisableDepthTest;
			key.CullMode = entity->ForceWireframe ? nvrhi::RasterCullMode::None : entity->CullMode;
		}

		packet.Pipeline = m_PipelineCache.GetOrCreatePipeline(key, fbInfo);

		view->Queues.Opaque.push_back(packet);
	}
}

// --- RenderPacket creation ---

Renderer::RenderPacket Engine::SceneRenderer::CreateRenderPacket(const Renderer::Entity* entity, nvrhi::BufferHandle perObjectBuffer, nvrhi::BufferRange perObjectRange) {
	Renderer::RenderPacket packet;
	packet.VertexBuffer = entity->mesh->GetVertexBuffer();
	packet.IndexBuffer = entity->mesh->GetIndexBuffer();
	packet.VertexCount = entity->mesh->GetVertexCount();
	packet.IndexCount = entity->mesh->GetIndexCount();
	packet.MaterialBindings = entity->material->GetBindingSet();
	packet.BaseVertexOffset = 0;
	packet.StartIndexLocation = 0;

	nvrhi::BindingSetDesc setDesc;
	setDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, perObjectBuffer, perObjectRange));
	packet.PerObjectBindings = m_Device->createBindingSet(setDesc, m_PerObjectBindingLayout);

	return packet;
}

void Engine::SceneRenderer::SortViewQueues(Renderer::RenderView* view) {
	if (!view) return;

	const auto lessBySubmissionState = [](const Renderer::RenderPacket& left, const Renderer::RenderPacket& right) {
		if (left.Pipeline.Get() != right.Pipeline.Get()) return left.Pipeline.Get() < right.Pipeline.Get();
		if (left.MaterialBindings.Get() != right.MaterialBindings.Get()) return left.MaterialBindings.Get() < right.MaterialBindings.Get();
		if (left.VertexBuffer.Get() != right.VertexBuffer.Get()) return left.VertexBuffer.Get() < right.VertexBuffer.Get();
		return left.IndexBuffer.Get() < right.IndexBuffer.Get();
	};

	std::stable_sort(view->Queues.Opaque.begin(), view->Queues.Opaque.end(), lessBySubmissionState);
	std::stable_sort(view->Queues.Masked.begin(), view->Queues.Masked.end(), lessBySubmissionState);
	std::stable_sort(view->Queues.Transparent.begin(), view->Queues.Transparent.end(), lessBySubmissionState);
}

// --- View Building ---

void Engine::SceneRenderer::BuildViews(Renderer::FrameBlackboard& blackboard, nvrhi::ICommandList* commandList) {
	if (!commandList) {
		return;
	}

	m_SubmissionStats = {};

	const auto& shadowViews = blackboard.GetShadowViews();
	for (uint32_t viewIndex = 0; viewIndex < shadowViews.size(); ++viewIndex) {
		Renderer::RenderView* view = shadowViews[viewIndex];
		if (view) {
			view->Clear();
			CullAndFillView(view, commandList, viewIndex);
			SortViewQueues(view);
		}
	}

	const auto& sceneViews = blackboard.GetSceneViews();
	for (uint32_t viewIndex = 0; viewIndex < sceneViews.size(); ++viewIndex) {
		Renderer::RenderView* view = sceneViews[viewIndex];
		if (view) {
			view->Clear();
			CullAndFillView(view, commandList, static_cast<uint32_t>(shadowViews.size()) + viewIndex);
			SortViewQueues(view);
		}
	}
}

void Engine::SceneRenderer::BuildShadowData(Engine::Scene* scene, Renderer::FrameBlackboard& blackboard) {
	if (!scene) return;

	const auto& shadowViews = blackboard.GetShadowViews();
	uint32_t viewIdx = 0;

	for (uint32_t idx = 0; idx < scene->GetLights().size(); ++idx) {
		if (viewIdx >= MaxShadowedLights || viewIdx >= shadowViews.size()) break;

		auto lightData = scene->GetLights()[idx]->GetDirectionalLightData();
		const bool usePosition = lightData.ColorUsePosition.w > 0.5f;
		const bool castShadows = lightData.ShadowParams.w > 0.5f;
		if (!usePosition || !castShadows) continue;

		const DirectX::XMMATRIX lightViewProj = ComputeSpotLightViewProj(lightData);

		Renderer::RenderView* currentShadowView = shadowViews[viewIdx];
		currentShadowView->ViewProjection = lightViewProj;
		currentShadowView->ExtractFrustumPlanes();

		currentShadowView->TargetScene = scene;

		viewIdx++;
	}

	blackboard.Set<uint32_t>("ShadowMapSize", ShadowMapSize);
	blackboard.Set<uint32_t>("MaxShadowedLights", MaxShadowedLights);
}

void Engine::SceneRenderer::BuildLighting(Engine::Scene* scene, Renderer::FrameBlackboard& blackboard) {
	if (!scene) return;

	std::vector<Renderer::DirectionalLightData> lights;
	lights.reserve(scene->GetLights().size());

	const auto& shadowViews = blackboard.GetShadowViews();
	uint32_t shadowIdx = 0;

	for (const auto& light : scene->GetLights()) {
		if (lights.size() >= MaxLights) {
			break;
		}

		auto lightData = light->GetDirectionalLightData();

		const bool usePosition = lightData.ColorUsePosition.w > 0.5f;
		const bool castShadows = lightData.ShadowParams.w > 0.5f;

		if (usePosition && castShadows && shadowIdx < shadowViews.size() && shadowIdx < MaxShadowedLights) {
			DirectX::XMStoreFloat4x4(&lightData.LightViewProj, DirectX::XMMatrixTranspose(shadowViews[shadowIdx]->ViewProjection));
			lightData.ShadowParams.x = static_cast<float>(shadowIdx);
			shadowIdx++;
		}

		lights.push_back(lightData);
	}

	blackboard.SetDirectionalLights(std::move(lights));
}

#include "SceneRenderer.h"

#include <algorithm>
#include <cmath>

#include "renderer/Entity.h"
#include "renderer/Mesh.h"
#include "renderer/Material.h"
#include "Scene.h"

namespace {
	struct PerObjectData {
		DirectX::XMFLOAT4X4 MVP;
		DirectX::XMFLOAT4X4 World;
		DirectX::XMFLOAT4 ObjectParams;
	};

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

	nvrhi::BufferDesc perObjectDesc;
	perObjectDesc.byteSize = MaxPerObjects * PerObjectDataStride;
	perObjectDesc.isConstantBuffer = true;
	perObjectDesc.isVolatile = true;
	perObjectDesc.maxVersions = 3;

	perObjectDesc.debugName = "PerObjectDataBuffer";
	m_PerObjectDataBuffer = device->createBuffer(perObjectDesc);

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

void Engine::SceneRenderer::BeginUploads() {
	if (!m_UploadCommandList) {
		m_UploadCommandList = m_Device->createCommandList();
	}
	if (!m_UploadsPending) {
		m_UploadCommandList->open();
		m_UploadsPending = true;
	}
}

void Engine::SceneRenderer::FlushUploadsIfPending() {
	if (m_UploadsPending) {
		m_UploadCommandList->close();
		m_Device->executeCommandList(m_UploadCommandList);
		m_UploadsPending = false;
	}
}


void Engine::SceneRenderer::CullAndFillView(Renderer::RenderView* view) {
	if (!view || !view->TargetScene) return;

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

	for (const auto& entity : view->TargetScene->GetEntities()) {
		if (!entity->mesh || !entity->material) continue;

		if (view->ViewType == Renderer::RenderView::Type::Shadow && !entity->CastShadows) continue;

		// TODO: Frustum Culling


		uint32_t offset = m_PerObjectDataOffset;
		m_PerObjectDataOffset += PerObjectDataStride;

		PerObjectData perObjectData{};
		DirectX::XMMATRIX world = entity->transform.GetWorldMat();
		DirectX::XMMATRIX mvp = world * viewProj;

		DirectX::XMStoreFloat4x4(&perObjectData.World, DirectX::XMMatrixTranspose(world));
		DirectX::XMStoreFloat4x4(&perObjectData.MVP, DirectX::XMMatrixTranspose(mvp));

		perObjectData.ObjectParams = {
			entity->ReceiveShadows ? 1.0f : 0.0f,
			entity->ForceWireframe ? 1.0f : 0.0f,
			0.0f, 0.0f
		};

		m_UploadCommandList->writeBuffer(m_PerObjectDataBuffer, &perObjectData, sizeof(PerObjectData), offset);

		auto packet = CreateRenderPacket(entity.get(), offset);
		packet.Queue = Renderer::RenderQueueType::Opaque;

		Engine::PipelineKey key;
		key.VS = m_MaterialSystem->GetStandardVS();
		key.PerObjectLayout = m_PerObjectBindingLayout;
		key.SceneLayout = m_SceneBindingLayout;
		key.MaterialLayout = m_MaterialSystem->GetStandardMaterialLayout();
		key.DepthWrite = true;

		if (view->ViewType == Renderer::RenderView::Type::Shadow) {
			key.PS = nullptr;
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

Renderer::RenderPacket Engine::SceneRenderer::CreateRenderPacket(const Renderer::Entity* entity, uint32_t perObjectOffset) {
	Renderer::RenderPacket packet;
	packet.VertexBuffer = entity->mesh->GetVertexBuffer();
	packet.IndexBuffer = entity->mesh->GetIndexBuffer();
	packet.VertexCount = entity->mesh->GetVertexCount();
	packet.IndexCount = entity->mesh->GetIndexCount();
	packet.MaterialBindings = entity->material->GetBindingSet();
	packet.BaseVertexOffset = 0;
	packet.StartIndexLocation = 0;

	nvrhi::BindingSetDesc setDesc;
	setDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(
		0,
		m_PerObjectDataBuffer,
		nvrhi::BufferRange{ perObjectOffset, sizeof(PerObjectData) }
	));
	packet.PerObjectBindings = m_Device->createBindingSet(setDesc, m_PerObjectBindingLayout);

	return packet;
}

// --- View Building ---

void Engine::SceneRenderer::BuildViews(Renderer::FrameBlackboard& blackboard) {
	BeginUploads();

	auto shadowViews = blackboard.GetShadowViews();
	for (Renderer::RenderView* view : shadowViews) {
		if (view) {
			view->Clear();
			CullAndFillView(view);
		}
	}

	auto sceneViews = blackboard.GetSceneViews();
	for (Renderer::RenderView* view : sceneViews) {
		if (view) {
			view->Clear();
			CullAndFillView(view);
		}
	}

	FlushUploadsIfPending();
}

void Engine::SceneRenderer::BuildShadowData(Engine::Scene* scene, Renderer::FrameBlackboard& blackboard) {
	if (!scene) return;

	auto shadowViews = blackboard.GetShadowViews();
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

	auto shadowViews = blackboard.GetShadowViews();
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

#include "SceneRenderer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#include "renderer/Entity.h"
#include "renderer/Mesh.h"
#include "renderer/Material.h"
#include "Scene.h"
#include "renderer/DebugGeometry.h"

namespace {
	struct PerObjectData {
		DirectX::XMFLOAT4X4 MVP;
		DirectX::XMFLOAT4X4 World;
		DirectX::XMFLOAT4 ObjectParams;
		uint8_t Padding[112]{};
	};

	struct DebugViewData {
		DirectX::XMFLOAT4X4 ViewProjection;
	};

	DirectX::XMVECTOR NormalizeOrDefault(const DirectX::XMFLOAT3& value) {
		DirectX::XMVECTOR vector = DirectX::XMLoadFloat3(&value);
		const float lengthSquared = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(vector));
		if (!std::isfinite(lengthSquared) || lengthSquared < 1e-6f) {
			return DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
		}
		return DirectX::XMVector3Normalize(vector);
	}

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

void Engine::SceneRenderer::Init(nvrhi::IDevice* device,
	Renderer::MaterialSystem* materialSystem,
	Renderer::ShaderFactory* shaderFactory) {
	m_MaterialSystem = materialSystem;
	m_PipelineCache.Init(device);

	m_Device = device;

	if (shaderFactory) {
		m_DebugVS = shaderFactory->GetShader("shaders/debug_vs.dxbc", nvrhi::ShaderType::Vertex);
		m_DebugPS = shaderFactory->GetShader("shaders/debug_ps.dxbc", nvrhi::ShaderType::Pixel);
	}

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
		nvrhi::BindingLayoutItem::Sampler(3),
		nvrhi::BindingLayoutItem::ConstantBuffer(4)
	};
	m_SceneBindingLayout = device->createBindingLayout(sceneLayoutDesc);

	nvrhi::BindingLayoutDesc debugLayoutDesc;
	debugLayoutDesc.visibility = nvrhi::ShaderType::Vertex;
	debugLayoutDesc.bindings = {
		nvrhi::BindingLayoutItem::ConstantBuffer(0)
	};
	m_DebugBindingLayout = device->createBindingLayout(debugLayoutDesc);

	nvrhi::BufferDesc debugViewDesc;
	debugViewDesc.byteSize = sizeof(DebugViewData);
	debugViewDesc.isConstantBuffer = true;
	debugViewDesc.isVolatile = true;
	debugViewDesc.maxVersions = 8;
	debugViewDesc.debugName = "DebugView_Buffer";
	m_DebugViewBuffer = device->createBuffer(debugViewDesc);

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

void Engine::SceneRenderer::EnsureDebugBuffer(nvrhi::BufferHandle& buffer,
	uint64_t& capacity,
	size_t vertexCount,
	const char* debugName) {
	if (vertexCount == 0) {
		return;
	}

	const uint64_t requiredBytes = static_cast<uint64_t>(vertexCount) * sizeof(Renderer::DebugVertex);
	if (buffer && capacity >= requiredBytes) {
		return;
	}

	uint64_t newCapacity = (std::max)(requiredBytes, static_cast<uint64_t>(sizeof(Renderer::DebugVertex) * 256));
	while (newCapacity < requiredBytes && newCapacity <= UINT64_MAX / 2) {
		newCapacity *= 2;
	}

	nvrhi::BufferDesc desc;
	desc.byteSize = newCapacity;
	desc.isVertexBuffer = true;
	desc.initialState = nvrhi::ResourceStates::VertexBuffer;
	desc.keepInitialState = true;
	desc.debugName = debugName ? debugName : "DebugGeometry_Buffer";
	nvrhi::BufferHandle newBuffer = m_Device ? m_Device->createBuffer(desc) : nullptr;
	if (newBuffer) {
		buffer = std::move(newBuffer);
		capacity = newCapacity;
	}
}

nvrhi::GraphicsPipelineHandle Engine::SceneRenderer::GetDebugPipeline(
	const nvrhi::FramebufferInfo& framebuffer,
	nvrhi::PrimitiveType primitiveType,
	Renderer::DebugDepthMode depthMode) {
	if (!m_DebugVS || !m_DebugPS || !m_DebugBindingLayout) {
		return nullptr;
	}

	Engine::PipelineKey key;
	key.VS = m_DebugVS;
	key.PS = m_DebugPS;
	key.PerObjectLayout = m_DebugBindingLayout;
	key.PrimType = primitiveType;
	key.InputLayout = {
		nvrhi::VertexAttributeDesc()
			.setName("POSITION")
			.setFormat(nvrhi::Format::RGB32_FLOAT)
			.setOffset(offsetof(Renderer::DebugVertex, Position))
			.setElementStride(sizeof(Renderer::DebugVertex)),
		nvrhi::VertexAttributeDesc()
			.setName("COLOR")
			.setFormat(nvrhi::Format::RGBA32_FLOAT)
			.setOffset(offsetof(Renderer::DebugVertex, Color))
			.setElementStride(sizeof(Renderer::DebugVertex))
	};
	key.DepthTest = depthMode == Renderer::DebugDepthMode::Test;
	key.DepthWrite = false;
	key.CullMode = nvrhi::RasterCullMode::None;
	key.RasterState.setCullNone();
	key.RasterState.setAntialiasedLineEnable(primitiveType == nvrhi::PrimitiveType::LineList);

	nvrhi::BlendState::RenderTarget blendTarget;
	blendTarget.enableBlend()
		.setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
		.setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
		.setSrcBlendAlpha(nvrhi::BlendFactor::One)
		.setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
	key.BlendState.setRenderTarget(0, blendTarget);

	return m_PipelineCache.GetOrCreatePipeline(key, framebuffer);
}

void Engine::SceneRenderer::RenderDebugGeometry(nvrhi::ICommandList* commandList,
	const Renderer::RenderView& view) {
	if (!commandList || !view.TargetFramebuffer || view.DebugGeometryData.Empty() ||
		!m_DebugViewBuffer || !m_DebugBindingLayout) {
		return;
	}

	const Renderer::DebugGeometry& geometry = view.DebugGeometryData;
	EnsureDebugBuffer(m_DebugLineBuffer,
		m_DebugLineBufferCapacity,
		geometry.GetLineVertices().size(),
		"DebugLineVertices");
	EnsureDebugBuffer(m_DebugTriangleBuffer,
		m_DebugTriangleBufferCapacity,
		geometry.GetTriangleVertices().size(),
		"DebugTriangleVertices");

	if ((!geometry.GetLineVertices().empty() && !m_DebugLineBuffer) ||
		(!geometry.GetTriangleVertices().empty() && !m_DebugTriangleBuffer)) {
		return;
	}

	DebugViewData viewData;
	DirectX::XMStoreFloat4x4(&viewData.ViewProjection,
		DirectX::XMMatrixTranspose(view.ViewProjection));
	commandList->writeBuffer(m_DebugViewBuffer, &viewData, sizeof(viewData));

	nvrhi::BindingSetDesc bindingDesc;
	bindingDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_DebugViewBuffer));
	nvrhi::BindingSetHandle bindings = m_Device->createBindingSet(bindingDesc, m_DebugBindingLayout);
	if (!bindings) {
		return;
	}

	const nvrhi::FramebufferInfo framebufferInfo = view.TargetFramebuffer->getFramebufferInfo();
	const nvrhi::Viewport viewport = nvrhi::Viewport(
		static_cast<float>(view.ViewportWidth),
		static_cast<float>(view.ViewportHeight));
	const nvrhi::Rect scissor = nvrhi::Rect(view.ViewportWidth, view.ViewportHeight);

	const auto drawVertices = [&](const std::vector<Renderer::DebugVertex>& vertices,
		nvrhi::BufferHandle vertexBuffer,
		nvrhi::PrimitiveType primitiveType) {
		if (vertices.empty() || !vertexBuffer) {
			return;
		}

		commandList->writeBuffer(vertexBuffer, vertices.data(), vertices.size() * sizeof(Renderer::DebugVertex));
		nvrhi::GraphicsPipelineHandle pipeline = GetDebugPipeline(
			framebufferInfo,
			primitiveType,
			view.DebugSettings.DepthMode);
		if (!pipeline) {
			return;
		}

		nvrhi::GraphicsState state;
		state.pipeline = pipeline;
		state.framebuffer = view.TargetFramebuffer;
		state.viewport.addViewport(viewport);
		state.viewport.addScissorRect(scissor);
		state.bindings = { bindings };
		state.vertexBuffers = { nvrhi::VertexBufferBinding{ vertexBuffer, 0, 0 } };
		commandList->setGraphicsState(state);

		nvrhi::DrawArguments arguments;
		arguments.vertexCount = static_cast<uint32_t>(vertices.size());
		arguments.instanceCount = 1;
		commandList->draw(arguments);
	};

	drawVertices(geometry.GetTriangleVertices(), m_DebugTriangleBuffer, nvrhi::PrimitiveType::TriangleList);
	drawVertices(geometry.GetLineVertices(), m_DebugLineBuffer, nvrhi::PrimitiveType::LineList);
}

std::optional<Engine::LightId> Engine::SceneRenderer::PickLight(
	const Renderer::RenderView& view,
	float viewportX,
	float viewportY) const {
	if (view.ViewportWidth == 0 || view.ViewportHeight == 0 ||
		viewportX < 0.0f || viewportY < 0.0f ||
		viewportX >= static_cast<float>(view.ViewportWidth) ||
		viewportY >= static_cast<float>(view.ViewportHeight)) {
		return std::nullopt;
	}

	const float ndcX = viewportX / static_cast<float>(view.ViewportWidth) * 2.0f - 1.0f;
	const float ndcY = 1.0f - viewportY / static_cast<float>(view.ViewportHeight) * 2.0f;
	const DirectX::XMMATRIX inverseViewProjection = DirectX::XMMatrixInverse(nullptr, view.ViewProjection);
	const DirectX::XMVECTOR clipNear = DirectX::XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
	const DirectX::XMVECTOR clipFar = DirectX::XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);
	DirectX::XMVECTOR rayOrigin = DirectX::XMVector4Transform(clipNear, inverseViewProjection);
	DirectX::XMVECTOR rayEnd = DirectX::XMVector4Transform(clipFar, inverseViewProjection);
	const float originW = DirectX::XMVectorGetW(rayOrigin);
	const float endW = DirectX::XMVectorGetW(rayEnd);
	if (!std::isfinite(originW) || !std::isfinite(endW) ||
		std::abs(originW) < 1e-5f || std::abs(endW) < 1e-5f) {
		return std::nullopt;
	}

	rayOrigin = DirectX::XMVectorScale(rayOrigin, 1.0f / originW);
	rayEnd = DirectX::XMVectorScale(rayEnd, 1.0f / endW);
	const DirectX::XMVECTOR rayDirection = DirectX::XMVector3Normalize(
		DirectX::XMVectorSubtract(rayEnd, rayOrigin));
	const auto& targets = view.DebugGeometryData.GetLightTargets();
	float closestDistance = std::numeric_limits<float>::max();
	Engine::LightId closestLight = Engine::InvalidLightId;

	for (const Renderer::DebugLightTarget& target : targets) {
		const DirectX::XMVECTOR axis = NormalizeOrDefault(target.Direction);
		const DirectX::XMVECTOR offset = DirectX::XMVectorSubtract(
			rayOrigin,
			DirectX::XMLoadFloat3(&target.Position));
		const float axisRayDot = DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, rayDirection));
		const float axisOffsetDot = DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, offset));

		const float safeRange = std::max(target.Range, 0.25f);
		const float coneSlope = std::tan(std::clamp(target.OuterAngleRadians, 0.0f, DirectX::XM_PIDIV2 - 0.001f));
		const float quadraticA = 1.0f - axisRayDot * axisRayDot * (1.0f + coneSlope * coneSlope);
		const float quadraticB = 2.0f * (DirectX::XMVectorGetX(DirectX::XMVector3Dot(offset, rayDirection)) -
			axisOffsetDot * axisRayDot * (1.0f + coneSlope * coneSlope));
		const float quadraticC = DirectX::XMVectorGetX(DirectX::XMVector3Dot(offset, offset)) -
			axisOffsetDot * axisOffsetDot * (1.0f + coneSlope * coneSlope);

		float hitDistance = std::numeric_limits<float>::max();
		if (std::abs(quadraticA) < 1e-6f) {
			if (std::abs(quadraticB) > 1e-6f) {
				hitDistance = -quadraticC / quadraticB;
			}
		}
		else {
			const float discriminant = quadraticB * quadraticB - 4.0f * quadraticA * quadraticC;
			if (discriminant >= 0.0f) {
				const float sqrtDiscriminant = std::sqrt(discriminant);
				const float rootA = (-quadraticB - sqrtDiscriminant) / (2.0f * quadraticA);
				const float rootB = (-quadraticB + sqrtDiscriminant) / (2.0f * quadraticA);
				if (rootA > 0.0f) hitDistance = rootA;
				if (rootB > 0.0f) hitDistance = std::min(hitDistance, rootB);
			}
		}

		if (hitDistance == std::numeric_limits<float>::max()) {
			const float sphereRadius = std::max(0.2f, safeRange * 0.025f);
			const float sphereA = DirectX::XMVectorGetX(DirectX::XMVector3Dot(rayDirection, rayDirection));
			const float sphereB = 2.0f * DirectX::XMVectorGetX(DirectX::XMVector3Dot(offset, rayDirection));
			const float sphereC = DirectX::XMVectorGetX(DirectX::XMVector3Dot(offset, offset)) - sphereRadius * sphereRadius;
			const float sphereDiscriminant = sphereB * sphereB - 4.0f * sphereA * sphereC;
			if (sphereDiscriminant >= 0.0f) {
				const float sphereRoot = (-sphereB - std::sqrt(sphereDiscriminant)) / (2.0f * sphereA);
				if (sphereRoot > 0.0f) hitDistance = sphereRoot;
			}
		}

		if (hitDistance <= 0.0f || hitDistance >= std::numeric_limits<float>::max()) {
			continue;
		}

		const DirectX::XMVECTOR hitPoint = DirectX::XMVectorAdd(
			rayOrigin,
			DirectX::XMVectorScale(rayDirection, hitDistance));
		const float hitAxialDistance = DirectX::XMVectorGetX(DirectX::XMVector3Dot(
			DirectX::XMVectorSubtract(hitPoint, DirectX::XMLoadFloat3(&target.Position)), axis));
		if (hitAxialDistance < -0.25f || hitAxialDistance > safeRange + 0.25f) {
			continue;
		}

		if (hitDistance < closestDistance) {
			closestDistance = hitDistance;
			closestLight = target.Id;
		}
	}

	return closestLight == Engine::InvalidLightId
		? std::nullopt
		: std::optional<Engine::LightId>(closestLight);
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

	for (const auto& entity : view->TargetScene->GetEntities()) {
		if (!entity || !entity->mesh || !entity->material) {
			++m_SubmissionStats.ObjectsCulled;
			continue;
		}

		if (view->ViewType == Renderer::RenderView::Type::Shadow && !entity->CastShadows) {
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

		nvrhi::BufferDesc perObjectDesc;
		perObjectDesc.byteSize = PerObjectDataStride;
		perObjectDesc.isConstantBuffer = true;
		perObjectDesc.debugName = "PerObjectData";
		nvrhi::BufferHandle perObjectBuffer = m_Device->createBuffer(perObjectDesc);
		if (!perObjectBuffer) {
			std::cerr << "ERROR: Could not allocate per-object buffer for view " << viewIndex << std::endl;
			continue;
		}

		commandList->writeBuffer(perObjectBuffer, &perObjectData, PerObjectDataStride);

		auto packet = CreateRenderPacket(entity.get(), perObjectBuffer);
		packet.Queue = Renderer::RenderQueueType::Opaque;
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

Renderer::RenderPacket Engine::SceneRenderer::CreateRenderPacket(const Renderer::Entity* entity, nvrhi::BufferHandle perObjectBuffer) {
	Renderer::RenderPacket packet;
	packet.VertexBuffer = entity->mesh->GetVertexBuffer();
	packet.IndexBuffer = entity->mesh->GetIndexBuffer();
	packet.VertexCount = entity->mesh->GetVertexCount();
	packet.IndexCount = entity->mesh->GetIndexCount();
	packet.MaterialBindings = entity->material->GetBindingSet();
	packet.BaseVertexOffset = 0;
	packet.StartIndexLocation = 0;

	nvrhi::BindingSetDesc setDesc;
	setDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, perObjectBuffer));
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
			if (!view->CameraBuffer) {
				nvrhi::BufferDesc cameraBufferDesc;
				cameraBufferDesc.byteSize = sizeof(Renderer::CameraGpuData);
				cameraBufferDesc.isConstantBuffer = true;
				cameraBufferDesc.debugName = "ViewCameraPosition";
				view->CameraBuffer = m_Device->createBuffer(cameraBufferDesc);
			}
			if (view->CameraBuffer) {
				const Renderer::CameraGpuData cameraData{
					{ view->CameraPosition.x, view->CameraPosition.y, view->CameraPosition.z, 1.0f }
				};
				commandList->writeBuffer(view->CameraBuffer, &cameraData, sizeof(cameraData));
			}
			CullAndFillView(view, commandList, static_cast<uint32_t>(shadowViews.size()) + viewIndex);
			SortViewQueues(view);
			BuildDebugGeometry(view);
		}
	}
}

void Engine::SceneRenderer::BuildDebugGeometry(Renderer::RenderView* view) {
	if (!view || view->ViewType != Renderer::RenderView::Type::Main || !view->TargetScene) {
		return;
	}

	Renderer::DebugGeometryBuilder::BuildScene(
		*view->TargetScene,
		view->DebugSettings,
		view->DebugGeometryData);
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

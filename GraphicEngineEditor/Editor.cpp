#include "Editor.h"

#include "renderer/Light.h"
#include "panels/HierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/ScenePanel.h"
#include "panels/MainBar.h"
#include "panels/StatisticsPanel.h"
#include "events/EditorEvents.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>

void Editor::OnInit() {
	ImGui::SetCurrentContext(GetImGuiContext());

	// --- Create Core Scene ---
	m_ActiveScene = std::make_shared<Engine::Scene>();
	AddScene(m_ActiveScene);

	// --- Create and Bind Camera ---
	auto mainCamera = std::make_shared<Renderer::Camera>(m_CameraFov, m_CameraNearPlane, m_CameraFarPlane);
	mainCamera->m_Transform.SetPosition(0.0f, 0.0f, -5.0f);

	m_ActiveCameraId = m_ActiveScene->AddCamera(mainCamera);

	// --- Dummy Data Setup ---
	const DirectX::XMFLOAT3 objectPositions[2] = {
		DirectX::XMFLOAT3(-3.2f, 0.6f, 2.0f),
		DirectX::XMFLOAT3(3.2f, 0.3f, 2.3f)
	};

	const DirectX::XMFLOAT3 lightPositions[2] = {
		DirectX::XMFLOAT3(-3.2f, 1.8f, -0.4f),
		DirectX::XMFLOAT3(3.2f, 4.8f, -5.2f)
	};

	Renderer::MaterialProperties demoMaterialProperties;
	demoMaterialProperties.BaseColor = { 0.25f, 0.55f, 0.95f, 1.0f };
	demoMaterialProperties.Roughness = 0.45f;
	auto demoMaterial = GetRenderSystem()->GetMaterialSystem()->CreateMaterial(demoMaterialProperties);
	std::shared_ptr<Renderer::Entity> demoCube;
	ExecuteImmediate([this, demoMaterial, &demoCube](nvrhi::ICommandList* commandList) {
		Renderer::EntityBuilder builder(GetRenderer()->GetDevice(), commandList);
		demoCube = builder
			.Cube()
			.SetMaterial(demoMaterial)
			.Position(0.0f, 0.0f, 0.0f)
			.Build();
	});
	if (demoCube) {
		m_ActiveScene->AddEntity(demoCube);
	}

	const float lightRanges[2] = { 8.0f, 20.0f };
	const DirectX::XMFLOAT2 coneAngles[2] = {
		DirectX::XMFLOAT2(20.0f, 32.0f),
		DirectX::XMFLOAT2(30.0f, 46.0f)
	};

	for (int i = 0; i < 2; ++i) {
		const DirectX::XMFLOAT3 lightDirection(
			objectPositions[i].x - lightPositions[i].x,
			objectPositions[i].y - lightPositions[i].y,
			objectPositions[i].z - lightPositions[i].z
		);

		auto light = std::make_shared<Renderer::SpotLight>(lightDirection, DirectX::XMFLOAT3(1.0f, 0.96f, 0.9f), 8.5f);
		light->SetUsePosition(true);
		light->SetPosition(lightPositions[i]);
		light->SetDirection(lightDirection);
		light->SetRange(lightRanges[i]);
		light->SetConeAngles(coneAngles[i].x, coneAngles[i].y);
		light->SetCastShadows(true);
		light->SetShadowBias(0.0015f);
		light->SetShadowNormalBias(0.0025f);
		m_ActiveScene->AddLight(std::move(light));
	}

	// --- Create Viewport ---
	// Start with a default size (updated on resize/panel dock)
	CreateViewport(1280, 720);

	// --- Initialize Panels ---
	m_Windows.push_back(std::make_unique<MainBar>());
	m_Windows.push_back(std::make_unique<StatWindow>());
	auto scenePanel = std::make_unique<ScenePanel>();
	m_ScenePanel = scenePanel.get();
	m_Windows.push_back(std::move(scenePanel));
	m_Windows.push_back(std::make_unique<HierarchyPanel>());
	m_Windows.push_back(std::make_unique<InspectorPanel>());

	auto emitter = [this](std::unique_ptr<Engine::Events::Event> event) {
		this->ProcessPanelEvent(std::move(event));
		};

	for (auto& window : m_Windows) {
		if (window) {
			window->SetEventEmitter(emitter);
		}
	}

	// Broadcast initial state
	BroadcastSceneChanged();
	BroadcastCameraSettings();
	BroadcastViewportChanged();
}

void Editor::CreateViewport(uint32_t width, uint32_t height) {
	if (width == 0 || height == 0) {
		return;
	}

	DestroyViewport();

	auto* device = GetRenderer()->GetDevice();

	nvrhi::TextureDesc colorDesc;
	colorDesc.width = width;
	colorDesc.height = height;
	colorDesc.format = nvrhi::Format::RGBA8_UNORM;
	colorDesc.isRenderTarget = true;
	colorDesc.isShaderResource = true;
	colorDesc.initialState = nvrhi::ResourceStates::RenderTarget;
	colorDesc.keepInitialState = true;
	colorDesc.debugName = "ViewportColor";

	m_ViewportColorTexture = device->createTexture(colorDesc);
	if (!m_ViewportColorTexture) {
		std::cerr << "ERROR: Failed to create viewport color texture." << std::endl;
		return;
	}

	nvrhi::TextureDesc depthDesc;
	depthDesc.width = width;
	depthDesc.height = height;
	depthDesc.depth = 1;
	depthDesc.arraySize = 1;
	depthDesc.mipLevels = 1;
	depthDesc.format = nvrhi::Format::D24S8;
	depthDesc.sampleCount = 1;
	depthDesc.sampleQuality = 0;
	depthDesc.isRenderTarget = true;
	depthDesc.isUAV = false;
	depthDesc.isShaderResource = false;
	depthDesc.initialState = nvrhi::ResourceStates::DepthWrite;
	depthDesc.keepInitialState = true;
	depthDesc.debugName = "ViewportDepth";

	m_ViewportDepthTexture = device->createTexture(depthDesc);
	if (!m_ViewportDepthTexture) {
		std::cerr << "ERROR: Failed to create viewport depth texture." << std::endl;
		return;
	}

	nvrhi::FramebufferDesc framebufferDesc;
	framebufferDesc.addColorAttachment(m_ViewportColorTexture);
	framebufferDesc.setDepthAttachment(m_ViewportDepthTexture);
	m_ViewportFramebuffer = device->createFramebuffer(framebufferDesc);

	if (!m_ViewportFramebuffer) {
		std::cerr << "ERROR: Failed to create viewport framebuffer." << std::endl;
		return;
	}

	nvrhi::Object nativeView = m_ViewportColorTexture->getNativeView(nvrhi::ObjectTypes::D3D11_ShaderResourceView);
	m_ViewportSRV = static_cast<ID3D11ShaderResourceView*>(nativeView);

	m_ViewportSize = std::pair<float, float>(static_cast<float>(width), static_cast<float>(height));

	// Update or recreate the Application Viewport object with the new Framebuffer
	if (m_EditorViewport) {
		RemoveViewport(m_EditorViewport);
	}
	m_EditorViewport = std::make_shared<Engine::Viewport>(width, height, m_ViewportFramebuffer.Get());
	AddViewport(m_EditorViewport);
}

void Editor::DestroyViewport() {
	m_ViewportSRV.Reset();
	m_ViewportFramebuffer = nullptr;
	m_ViewportDepthTexture = nullptr;
	m_ViewportColorTexture = nullptr;
	m_ViewportSize = std::pair<float, float>(0.0f, 0.0f);
}

void* Editor::GetViewportTextureID() const {
	return m_ViewportSRV.Get();
}

void Editor::OnSetupViews() {
	// Rejestracja okienka Edytora w nowym potoku (Application -> RenderSystem)
	const auto activeCamera = GetActiveCamera();
	if (m_EditorViewport && activeCamera && m_ActiveScene) {
		Engine::ViewportBinding binding;
		binding.TargetViewport = m_EditorViewport.get();
		binding.SourceCamera = activeCamera.get();
		binding.TargetScene = m_ActiveScene.get();

		RegisterView(binding);
	}
}

void Editor::OnUpdate(float deltaTime) {
	const auto keyDown = [this](Engine::Events::KeyCode key) {
		return m_KeyDown[static_cast<size_t>(key)];
		};

	const auto mouseDown = [this](Engine::Events::MouseButton button) {
		return m_MouseButtonDown[static_cast<size_t>(button)];
		};

	const auto activeCamera = GetActiveCamera();
	if (activeCamera) {
		ImGuiIO& io = ImGui::GetIO();

		const bool rightMouseDown = mouseDown(Engine::Events::MouseButton::Right);
		const bool shouldHideCursor = rightMouseDown && m_ViewportHovered;
		if (GetWindow() && shouldHideCursor != m_CursorHidden) {
			GetWindow()->SetCursorVisible(!shouldHideCursor);
			m_CursorHidden = shouldHideCursor;
		}

		const bool viewportActive = m_ViewportHovered && !io.WantTextInput;
		if (viewportActive) {
			const float moveSpeed = 8.0f;
			const float step = moveSpeed * deltaTime;

			float moveForward = 0.0f;
			float moveRight = 0.0f;
			float moveUp = 0.0f;

			if (keyDown(Engine::Events::KeyCode::W)) moveForward += 1.0f;
			if (keyDown(Engine::Events::KeyCode::S)) moveForward -= 1.0f;
			if (keyDown(Engine::Events::KeyCode::D)) moveRight += 1.0f;
			if (keyDown(Engine::Events::KeyCode::A)) moveRight -= 1.0f;
			if (keyDown(Engine::Events::KeyCode::E)) moveUp += 1.0f;
			if (keyDown(Engine::Events::KeyCode::Q)) moveUp -= 1.0f;

			const float sinYaw = std::sin(m_Yaw);
			const float cosYaw = std::cos(m_Yaw);

			auto& position = activeCamera->m_Transform.Position;
			position.x += (sinYaw * moveForward + cosYaw * moveRight) * step;
			position.z += (cosYaw * moveForward - sinYaw * moveRight) * step;
			position.y += moveUp * step;
		}

		if (rightMouseDown && m_ViewportHovered) {
			const float sensitivity = 0.0035f;
			m_Yaw += m_MouseDeltaX * sensitivity;
			m_Pitch += m_MouseDeltaY * sensitivity;
			m_Pitch = std::clamp(m_Pitch, -1.5f, 1.5f);

			activeCamera->m_Transform.SetRotation(m_Pitch, m_Yaw, 0.0f);
		}
	}

	m_MouseDeltaX = 0.0f;
	m_MouseDeltaY = 0.0f;
}

void Editor::OnEvent(Engine::Events::Event& event) {
	for (auto& window : m_Windows) {
		if (window) {
			window->OnEvent(event);
			if (event.Handled) {
				return;
			}
		}
	}

	Engine::Events::EventDispatcher dispatcher(event);

	dispatcher.Dispatch<Engine::Events::KeyPressedEvent>([this](const auto& keyEvent) {
		constexpr int GLFW_MOD_CONTROL = 0x0002;
		if ((keyEvent.Mods & GLFW_MOD_CONTROL) == 0) {
			return false;
		}
		if (keyEvent.Key == Engine::Events::KeyCode::Z) {
			Undo();
			return true;
		}
		if (keyEvent.Key == Engine::Events::KeyCode::Y) {
			Redo();
			return true;
		}
		return false;
		});

	if (event.Handled) return;

	dispatcher.Dispatch<Engine::Events::KeyPressedEvent>([this](const auto& keyEvent) {
		m_KeyDown[static_cast<size_t>(keyEvent.Key)] = true;
		return false;
		});

	dispatcher.Dispatch<Engine::Events::KeyReleasedEvent>([this](const auto& keyEvent) {
		m_KeyDown[static_cast<size_t>(keyEvent.Key)] = false;
		return false;
		});

	dispatcher.Dispatch<Engine::Events::MouseButtonPressedEvent>([this](const auto& mouseButtonEvent) {
		m_MouseButtonDown[static_cast<size_t>(mouseButtonEvent.Button)] = true;
		return false;
		});

	dispatcher.Dispatch<Engine::Events::MouseButtonReleasedEvent>([this](const auto& mouseButtonEvent) {
		m_MouseButtonDown[static_cast<size_t>(mouseButtonEvent.Button)] = false;
		return false;
		});

	dispatcher.Dispatch<Engine::Events::MouseMovedEvent>([this](const auto& mouseMovedEvent) {
		m_MouseDeltaX += static_cast<float>(mouseMovedEvent.DeltaX);
		m_MouseDeltaY += static_cast<float>(mouseMovedEvent.DeltaY);
		return false;
		});

	dispatcher.Dispatch<Engine::Events::MouseScrolledEvent>([this](const auto& mouseScrolledEvent) {
		if (ImGui::GetIO().WantCaptureMouse || !m_ViewportHovered || !GetActiveCamera()) {
			return false;
		}

		constexpr float zoomSensitivity = 0.05f;
		const float     minFov = DirectX::XMConvertToRadians(20.0f);
		const float     maxFov = DirectX::XMConvertToRadians(90.0f);

		m_CameraFov -= static_cast<float>(mouseScrolledEvent.YOffset) * zoomSensitivity;
		m_CameraFov = std::clamp(m_CameraFov, minFov, maxFov);

		UpdateProjection();
		return true;
		});
}

void Editor::OnRender() {
	// Puste - RenderSystem automatycznie zajmie się rysowaniem na podstawie OnSetupViews
}

void Editor::OnResize(uint32_t width, uint32_t height) {
	if (width == 0 || height == 0) return;

	auto* defaultFramebuffer = GetRenderer()->GetDefaultFramebuffer();
	if (defaultFramebuffer) {
		const auto& info = defaultFramebuffer->getFramebufferInfo();
		CreateViewport(info.width, info.height);
	}

	UpdateProjection();
	BroadcastViewportChanged();
}

void Editor::UpdateProjection() {
	const uint32_t width = static_cast<uint32_t>(m_ViewportSize.first);
	const uint32_t height = static_cast<uint32_t>(m_ViewportSize.second);
	UpdateProjection(width, height);
}

void Editor::UpdateProjection(uint32_t width, uint32_t height) {
	(void)width;
	(void)height;
	const auto activeCamera = GetActiveCamera();
	if (activeCamera) {
		activeCamera->SetProjection(m_CameraFov, m_CameraNearPlane, m_CameraFarPlane);
	}
}

std::shared_ptr<Renderer::Camera> Editor::GetActiveCamera() const {
	return m_ActiveScene ? m_ActiveScene->FindCamera(m_ActiveCameraId) : nullptr;
}

void Editor::ProcessPanelEvent(std::unique_ptr<Engine::Events::Event> event) {
	if (!event) return;

	Engine::Events::EventDispatcher dispatcher(*event);

	dispatcher.Dispatch<EntitySelectedEvent>([this, &event](const auto& e) {
		m_SelectedItemType = static_cast<SelectedItemType>(e.GetItemType());
		m_SelectedEntityId = e.GetEntityId();
		m_SelectedLightId = e.GetLightId();
		m_SelectedCameraId = e.GetCameraId();

		const auto entity = m_ActiveScene ? m_ActiveScene->FindEntity(m_SelectedEntityId) : nullptr;
		const auto light = m_ActiveScene ? m_ActiveScene->FindLight(m_SelectedLightId) : nullptr;
		const auto camera = m_ActiveScene ? m_ActiveScene->FindCamera(m_SelectedCameraId) : nullptr;
		if (camera) {
			m_ActiveCameraId = m_SelectedCameraId;
		}

		EntitySelectedEvent enrichedEvent(
			static_cast<EntitySelectedEvent::ItemType>(m_SelectedItemType),
			e.GetObjectIndex(), e.GetLightIndex(), e.GetCameraIndex(),
			m_SelectedEntityId, m_SelectedLightId, m_SelectedCameraId,
			entity, light, camera
		);
		BroadcastEvent(enrichedEvent);
		return false;
		});

	dispatcher.Dispatch<EntityDeselectedEvent>([this, &event](const auto&) {
		m_SelectedItemType = SelectedItemType::None;
		m_SelectedEntityId = Engine::InvalidEntityId;
		m_SelectedLightId = Engine::InvalidLightId;
		m_SelectedCameraId = Engine::InvalidCameraId;
		BroadcastEvent(*event);
		return false;
		});

	dispatcher.Dispatch<AddObjectRequestedEvent>([this](const auto& e) {
		auto* device = GetRenderer()->GetDevice();
		DirectX::XMFLOAT3 spawnPosition = e.GetPosition();
		Renderer::MaterialProperties materialProperties;
		materialProperties.BaseColor = e.GetColor();
		materialProperties.Emissive = e.GetEmissive();
		materialProperties.Metallic = e.GetMetallic();
		materialProperties.Roughness = e.GetRoughness();
		auto material = GetRenderSystem()->GetMaterialSystem()->CreateMaterial(materialProperties);

		auto intersectsExisting = [this](const DirectX::XMFLOAT3& position) {
			if (!m_ActiveScene) return false;
			for (const auto& entity : m_ActiveScene->GetEntities()) {
				if (!entity) continue;
				const auto& existingPos = entity->transform.Position;
				const float dx = existingPos.x - position.x;
				const float dy = existingPos.y - position.y;
				const float dz = existingPos.z - position.z;
				if ((dx * dx + dy * dy + dz * dz) < 4.0f) return true;
			}
			return false;
			};

		for (int i = 0; i < 32 && intersectsExisting(spawnPosition); ++i) {
			spawnPosition.x += 2.5f;
		}

		std::shared_ptr<Renderer::Entity> createdEntity;
		ExecuteImmediate([device, spawnPosition, e, material, &createdEntity](nvrhi::ICommandList* cmd) {
			Renderer::EntityBuilder builder(device, cmd);

			switch (e.GetShape()) {
			case AddObjectRequestedEvent::Shape::Cube:   builder.Cube();   break;
			case AddObjectRequestedEvent::Shape::Sphere: builder.Sphere(); break;
			case AddObjectRequestedEvent::Shape::Plane:  builder.Plane();  break;
			case AddObjectRequestedEvent::Shape::Cone:   builder.Cone(20); break;
			}

			auto builtEntity = builder
				.Position(spawnPosition.x, spawnPosition.y, spawnPosition.z)
				.Scale(e.GetScale().x, e.GetScale().y, e.GetScale().z)
				.SetMaterial(material)
				.Build();
			createdEntity = std::move(builtEntity);
			});

		if (!createdEntity) {
			return false;
		}

		if (!m_ActiveScene) return false;
		const Engine::EntityId createdEntityId = m_ActiveScene->AddEntity(createdEntity);

		m_SelectedItemType = SelectedItemType::Object;
		m_SelectedEntityId = createdEntityId;
		m_SelectedLightId = Engine::InvalidLightId;
		m_SelectedCameraId = Engine::InvalidCameraId;
		const int createdEntityIndex = static_cast<int>(m_ActiveScene->GetEntityCount() - 1);

		BroadcastSceneChanged();

		EntitySelectedEvent enrichedEvent(
			EntitySelectedEvent::ItemType::Object,
			createdEntityIndex, -1, -1,
			createdEntityId, Engine::InvalidLightId, Engine::InvalidCameraId,
			createdEntity, {}, {}
		);
		BroadcastEvent(enrichedEvent);
		return false;
		});

	dispatcher.Dispatch<MaterialChangedEvent>([this](const auto& e) {
		auto entity = m_ActiveScene ? m_ActiveScene->FindEntity(e.GetEntityId()) : nullptr;
		if (!entity || !entity->material) return false;

		entity->material->SetProperties(e.GetProperties());
		return false;
		});

	dispatcher.Dispatch<CameraSettingsChangedEvent>([this](const auto& e) {
		m_CameraFov = e.GetFov();
		m_CameraNearPlane = e.GetNearPlane();
		m_CameraFarPlane = e.GetFarPlane();
		UpdateProjection();
		return false;
		});

	dispatcher.Dispatch<ViewportResizeRequestedEvent>([this](const auto& e) {
		CreateViewport(e.GetWidth(), e.GetHeight());
		UpdateProjection();
		BroadcastViewportChanged();
		return false;
		});

	dispatcher.Dispatch<ViewportHoveredEvent>([this](const auto& e) {
		m_ViewportHovered = e.IsHovered();
		return false;
		});

	dispatcher.Dispatch<CommandExecuteRequestedEvent>([this](auto& e) {
		auto cmd = e.TakeCommand();
		if (cmd) {
			m_CommandHistory.Execute(std::move(cmd));
		}
		return false;
		});

	dispatcher.Dispatch<DebugRenderToggledEvent>([this](const auto& e) {
		if (e.GetToggle() == DebugRenderToggledEvent::Toggle::LightGizmos) {
			m_ShowLightGizmos = e.GetValue();
		}
		return false;
		});
}

void Editor::BroadcastEvent(Engine::Events::Event& event) {
	for (auto& window : m_Windows) {
		if (window) {
			window->OnEvent(event);
		}
	}
}

void Editor::BroadcastSceneChanged() {
	std::vector<Engine::EntityId> entityIds;
	std::vector<Engine::LightId> lightIds;
	std::vector<Engine::CameraId> cameraIds;
	if (m_ActiveScene) {
		entityIds.reserve(m_ActiveScene->GetEntityCount());
		for (size_t i = 0; i < m_ActiveScene->GetEntityCount(); ++i) {
			entityIds.push_back(m_ActiveScene->GetEntityIdAt(i));
		}
		lightIds.reserve(m_ActiveScene->GetLights().size());
		for (size_t i = 0; i < m_ActiveScene->GetLights().size(); ++i) {
			lightIds.push_back(m_ActiveScene->GetLightIdAt(i));
		}
		cameraIds.reserve(m_ActiveScene->GetCameras().size());
		for (size_t i = 0; i < m_ActiveScene->GetCameras().size(); ++i) {
			cameraIds.push_back(m_ActiveScene->GetCameraIdAt(i));
		}
	}
	SceneChangedEvent event(std::move(entityIds), std::move(lightIds), std::move(cameraIds));
	BroadcastEvent(event);
}

void Editor::BroadcastCameraSettings() {
	CameraSettingsChangedEvent event(m_CameraFov, m_CameraNearPlane, m_CameraFarPlane);
	BroadcastEvent(event);
}

void Editor::BroadcastViewportChanged() {
	ViewportChangedEvent event(GetViewportTextureID(), m_ViewportSize.first, m_ViewportSize.second);
	BroadcastEvent(event);
}

void Editor::Undo() {
	if (m_CommandHistory.CanUndo()) {
		m_CommandHistory.Undo();
	}
}

void Editor::Redo() {
	if (m_CommandHistory.CanRedo()) {
		m_CommandHistory.Redo();
	}
}

void Editor::OnUIRender() {
	if (m_ActiveScene && m_SelectedEntityId != Engine::InvalidEntityId && !m_ActiveScene->FindEntity(m_SelectedEntityId)) {
		m_SelectedEntityId = Engine::InvalidEntityId;
		if (m_SelectedItemType == SelectedItemType::Object) m_SelectedItemType = SelectedItemType::None;
	}

	if (m_ActiveScene && m_SelectedLightId != Engine::InvalidLightId && !m_ActiveScene->FindLight(m_SelectedLightId)) {
		m_SelectedLightId = Engine::InvalidLightId;
		if (m_SelectedItemType == SelectedItemType::Light) m_SelectedItemType = SelectedItemType::None;
	}

	if (m_ActiveScene && m_SelectedCameraId != Engine::InvalidCameraId && !m_ActiveScene->FindCamera(m_SelectedCameraId)) {
		m_SelectedCameraId = Engine::InvalidCameraId;
		if (m_SelectedItemType == SelectedItemType::Camera) m_SelectedItemType = SelectedItemType::None;
	}

	ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
	ImGui::DockSpaceOverViewport(dockspaceId, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
	BuildDefaultDockLayoutIfNeeded(dockspaceId);

	if (m_ScenePanel) {
		std::vector<ScenePanel::LightGizmo> lightGizmos;
		const auto activeCamera = GetActiveCamera();
		if (m_ShowLightGizmos && m_ActiveScene && activeCamera && m_ViewportSize.first > 0.0f && m_ViewportSize.second > 0.0f) {
			const float aspect = m_ViewportSize.first / m_ViewportSize.second;
			const DirectX::XMMATRIX viewProjection = activeCamera->GetViewProjection(aspect);
			const auto projectToViewport = [&viewProjection](DirectX::FXMVECTOR worldPosition, DirectX::XMFLOAT2& normalizedPosition) {
				const DirectX::XMVECTOR clipPosition = DirectX::XMVector4Transform(worldPosition, viewProjection);
				const float clipW = DirectX::XMVectorGetW(clipPosition);
				if (!std::isfinite(clipW) || std::abs(clipW) < 1e-5f) {
					return false;
				}

				const float ndcX = DirectX::XMVectorGetX(clipPosition) / clipW;
				const float ndcY = DirectX::XMVectorGetY(clipPosition) / clipW;
				if (!std::isfinite(ndcX) || !std::isfinite(ndcY)) {
					return false;
				}

				normalizedPosition = {
					std::clamp(ndcX * 0.5f + 0.5f, 0.06f, 0.94f),
					std::clamp(-ndcY * 0.5f + 0.5f, 0.06f, 0.94f)
				};
				return true;
			};
			const auto lights = m_ActiveScene->GetLights();
			lightGizmos.reserve(lights.size());

			for (size_t index = 0; index < lights.size(); ++index) {
				const auto& light = lights[index];
				if (!light) continue;

				const DirectX::XMFLOAT3 position = light->GetPosition();
				const DirectX::XMVECTOR worldPosition = DirectX::XMVectorSet(position.x, position.y, position.z, 1.0f);

				ScenePanel::LightGizmo gizmo;
				gizmo.id = m_ActiveScene->GetLightIdAt(index);
				gizmo.index = static_cast<int>(index);
				if (!projectToViewport(worldPosition, gizmo.normalizedPosition)) continue;

				const DirectX::XMFLOAT3 direction = light->GetDirection();
				DirectX::XMVECTOR directionVector = DirectX::XMVectorSet(direction.x, direction.y, direction.z, 0.0f);
				const float directionLengthSquared = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(directionVector));
				if (!std::isfinite(directionLengthSquared) || directionLengthSquared < 1e-6f) {
					directionVector = DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
				}
				else {
					directionVector = DirectX::XMVector3Normalize(directionVector);
				}

				const float visualLength = std::clamp(light->GetRange() * 0.2f, 0.75f, 3.5f);
				const float coneRadius = visualLength * std::tan(DirectX::XMConvertToRadians(light->GetOuterAngleDegrees()));
				DirectX::XMVECTOR upReference = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
				if (std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(upReference, directionVector))) > 0.98f) {
					upReference = DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
				}
				const DirectX::XMVECTOR coneRight = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(directionVector, upReference));
				const DirectX::XMVECTOR coneBaseCenter = DirectX::XMVectorAdd(
					worldPosition,
					DirectX::XMVectorScale(directionVector, visualLength));
				const DirectX::XMVECTOR coneLeftPosition = DirectX::XMVectorAdd(
					coneBaseCenter,
					DirectX::XMVectorScale(coneRight, coneRadius));
				const DirectX::XMVECTOR coneRightPosition = DirectX::XMVectorSubtract(
					coneBaseCenter,
					DirectX::XMVectorScale(coneRight, coneRadius));
				gizmo.hasCone = projectToViewport(coneBaseCenter, gizmo.normalizedDirectionEnd) &&
					projectToViewport(coneLeftPosition, gizmo.normalizedConeLeft) &&
					projectToViewport(coneRightPosition, gizmo.normalizedConeRight);
				gizmo.color = light->GetColor();
				gizmo.selected = m_SelectedItemType == SelectedItemType::Light && m_SelectedLightId == gizmo.id;
				lightGizmos.push_back(gizmo);
			}
		}
		m_ScenePanel->SetLightGizmos(std::move(lightGizmos));
	}

	for (auto& window : m_Windows) {
		if (window) {
			window->Draw();
		}
	}
}

void Editor::BuildDefaultDockLayoutIfNeeded(ImGuiID dockspaceId) {
	if (m_DefaultLayoutBuilt) return;

	ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
	bool layoutNeedsRepair = false;
	const float hostWidth = GetWindow() ? static_cast<float>(GetWindow()->GetWidth()) : 0.0f;
	const float hostHeight = GetWindow() ? static_cast<float>(GetWindow()->GetHeight()) : 0.0f;
	if (node && hostWidth > 0.0f && hostHeight > 0.0f) {
		layoutNeedsRepair = node->Size.x > hostWidth * 1.5f || node->Size.y > hostHeight * 1.5f ||
			node->SizeRef.x > hostWidth * 1.5f || node->SizeRef.y > hostHeight * 1.5f;
		if (ImGuiWindow* inspectorWindow = ImGui::FindWindowByName("Inspector")) {
			layoutNeedsRepair = hostWidth > 0.0f && hostHeight > 0.0f &&
				(inspectorWindow->Pos.x < 0.0f || inspectorWindow->Pos.y < 0.0f ||
				 inspectorWindow->Pos.x + inspectorWindow->Size.x > hostWidth ||
				 inspectorWindow->Pos.y + inspectorWindow->Size.y > hostHeight);
		}
	}

	if (!node || layoutNeedsRepair) {
		BuildDefaultDockLayout(dockspaceId);
	} else if (node->Windows.Size > 0 || node->ChildNodes[0] != nullptr || node->ChildNodes[1] != nullptr) {
		m_DefaultLayoutBuilt = true;
		return;
	} else {
		BuildDefaultDockLayout(dockspaceId);
	}
	m_DefaultLayoutBuilt = true;
}

void Editor::BuildDefaultDockLayout(ImGuiID dockspaceId) {
	ImGui::DockBuilderRemoveNode(dockspaceId);
	ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

	ImGuiID dockMain = dockspaceId;
	ImGuiID dockLeft = 0;
	ImGuiID dockRight = 0;
	ImGuiID dockBottom = 0;

	ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, &dockLeft, &dockMain);
	ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.25f, &dockRight, &dockMain);
	ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.25f, &dockBottom, &dockMain);

	if (ImGuiDockNode* centralNode = ImGui::DockBuilderGetNode(dockMain)) {
		centralNode->LocalFlags |= ImGuiDockNodeFlags_CentralNode;
		centralNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
	}

	ImGui::DockBuilderDockWindow("Scene", dockLeft);
	ImGui::DockBuilderDockWindow("Inspector", dockRight);
	ImGui::DockBuilderDockWindow("Statistics", dockBottom);
	ImGui::DockBuilderDockWindow("Viewport", dockMain);
	ImGui::DockBuilderFinish(dockspaceId);
}

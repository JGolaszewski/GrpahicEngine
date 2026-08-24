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

	m_Cameras.clear();
	m_Cameras.push_back(mainCamera);
	m_ActiveCamera = mainCamera.get();

	m_ActiveScene->AddCamera(mainCamera);

	// --- Dummy Data Setup ---
	m_Cubes.clear();
	m_Lights.clear();

	const DirectX::XMFLOAT3 objectPositions[2] = {
		DirectX::XMFLOAT3(-3.2f, 0.6f, 2.0f),
		DirectX::XMFLOAT3(3.2f, 0.3f, 2.3f)
	};

	const DirectX::XMFLOAT3 lightPositions[2] = {
		DirectX::XMFLOAT3(-3.2f, 1.8f, -0.4f),
		DirectX::XMFLOAT3(3.2f, 4.8f, -5.2f)
	};

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

		Renderer::SpotLight light(lightDirection, DirectX::XMFLOAT3(1.0f, 0.96f, 0.9f), 8.5f);
		light.SetUsePosition(true);
		light.SetPosition(lightPositions[i]);
		light.SetDirection(lightDirection);
		light.SetRange(lightRanges[i]);
		light.SetConeAngles(coneAngles[i].x, coneAngles[i].y);
		light.SetCastShadows(true);
		light.SetShadowBias(0.0015f);
		light.SetShadowNormalBias(0.0025f);
		m_Lights.push_back(light);
	}

	// Populate the active scene
	for (auto& cube : m_Cubes) {
		m_ActiveScene->AddEntity(cube);
	}
	for (auto& light : m_Lights) {
		auto lightCopy = std::make_shared<Renderer::SpotLight>(light);
		m_ActiveScene->AddLight(lightCopy);
	}

	// --- Create Viewport ---
	// Start with a default size (updated on resize/panel dock)
	CreateViewport(1280, 720);

	// --- Initialize Panels ---
	m_Windows.push_back(std::make_unique<MainBar>());
	m_Windows.push_back(std::make_unique<StatWindow>());
	m_Windows.push_back(std::make_unique<ScenePanel>());
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
	if (m_EditorViewport && m_ActiveCamera && m_ActiveScene) {
		Engine::ViewportBinding binding;
		binding.TargetViewport = m_EditorViewport.get();
		binding.SourceCamera = m_ActiveCamera;
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

	if (m_ActiveCamera) {
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

			auto& position = m_ActiveCamera->m_Transform.Position;
			position.x += (sinYaw * moveForward + cosYaw * moveRight) * step;
			position.z += (cosYaw * moveForward - sinYaw * moveRight) * step;
			position.y += moveUp * step;
		}

		if (rightMouseDown && m_ViewportHovered) {
			const float sensitivity = 0.0035f;
			m_Yaw += m_MouseDeltaX * sensitivity;
			m_Pitch += m_MouseDeltaY * sensitivity;
			m_Pitch = std::clamp(m_Pitch, -1.5f, 1.5f);

			m_ActiveCamera->m_Transform.SetRotation(m_Pitch, m_Yaw, 0.0f);
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
		if (ImGui::GetIO().WantCaptureMouse || !m_ViewportHovered || !m_ActiveCamera) {
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
	// Nie robimy niczego na sztywno, Aspect Ratio jest przeliczany dynamicznie w RenderView
	// Ta metoda może pozostać do wysyłania powiadomień np. dla UI
}

void Editor::ProcessPanelEvent(std::unique_ptr<Engine::Events::Event> event) {
	if (!event) return;

	Engine::Events::EventDispatcher dispatcher(*event);

	dispatcher.Dispatch<EntitySelectedEvent>([this, &event](const auto& e) {
		m_SelectedItemType = static_cast<SelectedItemType>(e.GetItemType());
		m_SelectedObjectIndex = e.GetObjectIndex();
		m_SelectedLightIndex = e.GetLightIndex();
		m_SelectedCameraIndex = e.GetCameraIndex();

		Renderer::Entity* entity = nullptr;
		Renderer::SpotLight* light = nullptr;
		std::shared_ptr<Renderer::Camera> camera;

		if (m_SelectedItemType == SelectedItemType::Object && m_SelectedObjectIndex >= 0) {
			entity = m_Cubes[static_cast<size_t>(m_SelectedObjectIndex)].get();
		}
		else if (m_SelectedItemType == SelectedItemType::Light && m_SelectedLightIndex >= 0) {
			light = &m_Lights[static_cast<size_t>(m_SelectedLightIndex)];
		}
		else if (m_SelectedItemType == SelectedItemType::Camera && m_SelectedCameraIndex >= 0) {
			camera = m_Cameras[static_cast<size_t>(m_SelectedCameraIndex)];
			m_ActiveCamera = camera.get();
		}

		EntitySelectedEvent enrichedEvent(
			static_cast<EntitySelectedEvent::ItemType>(m_SelectedItemType),
			m_SelectedObjectIndex, m_SelectedLightIndex, m_SelectedCameraIndex,
			entity, light, camera
		);
		BroadcastEvent(enrichedEvent);
		return false;
		});

	dispatcher.Dispatch<EntityDeselectedEvent>([this, &event](const auto&) {
		m_SelectedItemType = SelectedItemType::None;
		m_SelectedObjectIndex = -1;
		m_SelectedLightIndex = -1;
		m_SelectedCameraIndex = -1;
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
			for (const auto& entity : m_Cubes) {
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

		m_Cubes.push_back(createdEntity);
		if (m_ActiveScene) {
			m_ActiveScene->AddEntity(createdEntity);
		}

		m_SelectedItemType = SelectedItemType::Object;
		m_SelectedObjectIndex = static_cast<int>(m_Cubes.size() - 1);
		m_SelectedLightIndex = -1;
		m_SelectedCameraIndex = -1;

		BroadcastSceneChanged();

		EntitySelectedEvent enrichedEvent(
			EntitySelectedEvent::ItemType::Object,
			m_SelectedObjectIndex, -1, -1,
			m_Cubes.back().get(), nullptr, nullptr
		);
		BroadcastEvent(enrichedEvent);
		return false;
		});

	dispatcher.Dispatch<MaterialChangedEvent>([this](const auto& e) {
		auto* entity = e.GetEntity();
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
}

void Editor::BroadcastEvent(Engine::Events::Event& event) {
	for (auto& window : m_Windows) {
		if (window) {
			window->OnEvent(event);
		}
	}
}

void Editor::BroadcastSceneChanged() {
	SceneChangedEvent event(m_Cubes.size(), m_Lights.size(), m_Cameras.size());
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
	if (m_SelectedObjectIndex >= static_cast<int>(m_Cubes.size())) {
		m_SelectedObjectIndex = -1;
		if (m_SelectedItemType == SelectedItemType::Object) m_SelectedItemType = SelectedItemType::None;
	}

	if (m_SelectedLightIndex >= static_cast<int>(m_Lights.size())) {
		m_SelectedLightIndex = -1;
		if (m_SelectedItemType == SelectedItemType::Light) m_SelectedItemType = SelectedItemType::None;
	}

	if (m_SelectedCameraIndex >= static_cast<int>(m_Cameras.size())) {
		m_SelectedCameraIndex = -1;
		if (m_SelectedItemType == SelectedItemType::Camera) m_SelectedItemType = SelectedItemType::None;
	}

	ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
	ImGui::DockSpaceOverViewport(dockspaceId, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
	BuildDefaultDockLayoutIfNeeded(dockspaceId);

	for (auto& window : m_Windows) {
		if (window) {
			window->Draw();
		}
	}
}

void Editor::BuildDefaultDockLayoutIfNeeded(ImGuiID dockspaceId) {
	if (m_DefaultLayoutBuilt) return;

	ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
	if (!node || node->Windows.Size > 0 || node->ChildNodes[0] != nullptr || node->ChildNodes[1] != nullptr) {
		m_DefaultLayoutBuilt = true;
		return;
	}

	BuildDefaultDockLayout(dockspaceId);
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

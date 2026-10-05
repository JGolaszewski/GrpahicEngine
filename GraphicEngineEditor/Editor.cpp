#include "Editor.h"

#include "Scene.h"
#include "PrimitiveFactory.h"
#include "ViewportTarget.h"
#include "renderer/Camera.h"
#include "renderer/Entity.h"
#include "renderer/Light.h"
#include "renderer/Material.h"
#include "renderer/Viewport.h"
#include "panels/HierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/ScenePanel.h"
#include "panels/MainBar.h"
#include "panels/StatisticsPanel.h"
#include "events/EditorEvents.h"

#include <DirectXMath.h>
#include <imgui.h>
#include <imgui_internal.h>

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
	demoMaterialProperties.Model = Renderer::BrdfModel::GGX;
	auto demoMaterial = GetRenderSystem()->GetMaterialSystem()->CreateMaterial(demoMaterialProperties);
	auto demoCube = GetRenderSystem()->CreatePrimitive(
		Engine::PrimitiveShape::Cube,
		demoMaterial,
		DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
	Engine::EntityId demoCubeId = Engine::InvalidEntityId;
	if (demoCube) {
		demoCube->Name = "Demo cube";
		demoCube->PrimitiveType = "cube";
		demoCubeId = m_ActiveScene->AddEntity(demoCube);
	}

	const float lightRanges[2] = { 8.0f, 20.0f };
	const DirectX::XMFLOAT2 coneAngles[2] = {
		DirectX::XMFLOAT2(20.0f, 32.0f),
		DirectX::XMFLOAT2(30.0f, 46.0f)
	};

	for (int i = 0; i < 2; ++i) {
		auto light = std::make_shared<Renderer::SpotLight>(
			DirectX::XMFLOAT3(0.0f, -1.0f, 0.0f),
			DirectX::XMFLOAT3(1.0f, 0.96f, 0.9f),
			8.5f);
		light->SetUsePosition(true);
		light->SetPosition(lightPositions[i]);
		light->LookAt(objectPositions[i]);
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
	if (demoCubeId != Engine::InvalidEntityId) {
		ProcessPanelEvent(std::make_unique<EntitySelectedEvent>(
			EntitySelectedEvent::ItemType::Object,
			0, -1, -1,
			demoCubeId, Engine::InvalidLightId, Engine::InvalidCameraId));
	}
}

void Editor::OnSceneReplaced(const std::shared_ptr<Engine::Scene>& scene) {
	m_ActiveScene = scene;
	m_ActiveCameraId = scene && !scene->GetCameras().empty()
		? scene->GetCameraIdAt(0)
		: Engine::InvalidCameraId;
	m_SelectedItemType = SelectedItemType::None;
	m_SelectedEntityId = Engine::InvalidEntityId;
	m_SelectedLightId = Engine::InvalidLightId;
	m_SelectedCameraId = Engine::InvalidCameraId;
	m_CommandHistory.Clear();
	if (const auto camera = GetActiveCamera()) {
		m_CameraFov = camera->GetFovRadians();
		m_CameraNearPlane = camera->GetNearPlane();
		m_CameraFarPlane = camera->GetFarPlane();
	}
	BroadcastSceneChanged();
	BroadcastCameraSettings();
	BroadcastViewportChanged();
}

void Editor::CreateViewport(uint32_t width, uint32_t height) {
	if (width == 0 || height == 0) {
		return;
	}

	DestroyViewport();

	if (!GetRenderSystem()) {
		return;
	}

	auto target = GetRenderSystem()->CreateViewportTarget(width, height);
	if (!target) {
		return;
	}

	m_ViewportSize = std::pair<float, float>(static_cast<float>(width), static_cast<float>(height));
	m_EditorViewportTarget = std::move(target);
	m_EditorViewport = std::make_shared<Engine::Viewport>(m_EditorViewportTarget);
	AddViewport(m_EditorViewport);
}

void Editor::DestroyViewport() {
	if (m_EditorViewport) {
		RemoveViewport(m_EditorViewport);
	}
	m_EditorViewport.reset();
	m_EditorViewportTarget.reset();
	m_ViewportSize = std::pair<float, float>(0.0f, 0.0f);
}

void* Editor::GetViewportTextureID() const {
	return m_EditorViewportTarget ? m_EditorViewportTarget->GetTextureId() : nullptr;
}

void Editor::OnSetupViews() {
	// Rejestracja okienka Edytora w nowym potoku (Application -> RenderSystem)
	const auto activeCamera = GetActiveCamera();
	if (m_EditorViewport && activeCamera && m_ActiveScene) {
		Engine::ViewportBinding binding;
		binding.TargetViewport = m_EditorViewport.get();
		binding.SourceCamera = activeCamera.get();
		binding.TargetScene = m_ActiveScene.get();
		binding.DebugSettings.ShowLightGizmos = m_ShowDebugLightGizmos;
		binding.DebugSettings.ShowWireframePlane = m_ShowDebugWireframePlane;
		binding.DebugSettings.DepthMode = Renderer::DebugDepthMode::AlwaysOnTop;
		binding.DebugSettings.SelectedLightId =
			m_SelectedItemType == SelectedItemType::Light ? m_SelectedLightId : Engine::InvalidLightId;

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
	const bool rightMouseDown = mouseDown(Engine::Events::MouseButton::Right);
	const ImGuiIO& io = ImGui::GetIO();

	Engine::EditorCameraController::InputState input;
	input.MoveForward = keyDown(Engine::Events::KeyCode::W);
	input.MoveBackward = keyDown(Engine::Events::KeyCode::S);
	input.MoveRight = keyDown(Engine::Events::KeyCode::D);
	input.MoveLeft = keyDown(Engine::Events::KeyCode::A);
	input.MoveUp = keyDown(Engine::Events::KeyCode::E);
	input.MoveDown = keyDown(Engine::Events::KeyCode::Q);
	input.RightMouseDown = rightMouseDown;
	input.ViewportHovered = m_ViewportHovered;
	input.TextInputActive = io.WantTextInput;

	m_CameraController.SetCamera(activeCamera.get());
	m_CameraController.SetInput(input);
	m_CameraController.Update(deltaTime);

	const bool shouldHideCursor = activeCamera && m_CameraController.ShouldCaptureCursor();
	if (GetWindow() && shouldHideCursor != m_CursorHidden) {
		GetWindow()->SetCursorVisible(!shouldHideCursor);
		m_CursorHidden = shouldHideCursor;
	}
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
		m_CameraController.AddMouseDelta(
			static_cast<float>(mouseMovedEvent.DeltaX),
			static_cast<float>(mouseMovedEvent.DeltaY));
		return false;
		});

	dispatcher.Dispatch<Engine::Events::MouseScrolledEvent>([this](const auto& mouseScrolledEvent) {
		if (ImGui::GetIO().WantCaptureMouse || !m_ViewportHovered) {
			return false;
		}

		const auto activeCamera = GetActiveCamera();
		m_CameraController.SetCamera(activeCamera.get());
		if (!m_CameraController.Zoom(static_cast<float>(mouseScrolledEvent.YOffset))) {
			return false;
		}

		m_CameraFov = activeCamera->GetFovRadians();
		UpdateProjection();
		return true;
		});
}

void Editor::OnRender() {
	// Puste - RenderSystem automatycznie zajmie się rysowaniem na podstawie OnSetupViews
}

void Editor::OnResize(uint32_t width, uint32_t height) {
	if (width == 0 || height == 0) return;

	CreateViewport(width, height);
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
		if (!m_ActiveScene || !GetRenderSystem()) {
			return false;
		}

		Renderer::MaterialProperties materialProperties;
		materialProperties.BaseColor = e.GetColor();
		materialProperties.Emissive = e.GetEmissive();
		materialProperties.Metallic = e.GetMetallic();
		materialProperties.Roughness = e.GetRoughness();
		auto material = GetRenderSystem()->GetMaterialSystem()->CreateMaterial(materialProperties);

		const DirectX::XMFLOAT3 spawnPosition = m_ActiveScene->FindFreeSpawnPosition(e.GetPosition());
		Engine::PrimitiveShape shape;
		const char* primitiveType = nullptr;
		switch (e.GetShape()) {
		case AddObjectRequestedEvent::Shape::Cube:   shape = Engine::PrimitiveShape::Cube; primitiveType = "cube"; break;
		case AddObjectRequestedEvent::Shape::Sphere: shape = Engine::PrimitiveShape::Sphere; primitiveType = "sphere"; break;
		case AddObjectRequestedEvent::Shape::Plane:  shape = Engine::PrimitiveShape::Plane; primitiveType = "plane"; break;
		case AddObjectRequestedEvent::Shape::Cone:   shape = Engine::PrimitiveShape::Cone; primitiveType = "cone"; break;
		default: return false;
		}

		auto createdEntity = GetRenderSystem()->CreatePrimitive(
			shape,
			std::move(material),
			spawnPosition,
			e.GetScale());

		if (!createdEntity) {
			return false;
		}

		if (!m_ActiveScene) return false;
		createdEntity->PrimitiveType = primitiveType;
		createdEntity->Name = primitiveType;
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

	dispatcher.Dispatch<ViewportClickedEvent>([this](const auto& e) {
		if (!GetRenderSystem() || !m_ActiveScene) {
			return false;
		}

		const auto lightId = GetRenderSystem()->PickLight(e.GetX(), e.GetY());
		if (!lightId) {
			return false;
		}

		ProcessPanelEvent(std::make_unique<EntitySelectedEvent>(
			EntitySelectedEvent::ItemType::Light,
			-1, -1, -1,
			Engine::InvalidEntityId, *lightId, Engine::InvalidCameraId));
		return true;
		});

	dispatcher.Dispatch<DebugRenderToggledEvent>([this](const auto& e) {
		switch (e.GetToggle()) {
		case DebugRenderToggledEvent::Toggle::LightGizmos:
			m_ShowDebugLightGizmos = e.GetValue();
			break;
		case DebugRenderToggledEvent::Toggle::WireframePlane:
			m_ShowDebugWireframePlane = e.GetValue();
			break;
		}
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

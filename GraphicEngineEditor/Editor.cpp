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
#include "panels/AssetBrowserPanel.h"
#include "events/EditorEvents.h"
#include "commands/EntityMembershipCommand.h"
#include "commands/ChangeMaterialTextureCommand.h"
#include "commands/ReplaceEntityCommand.h"
#include "platform/EditorFileDialogs.h"

#include <DirectXMath.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {
std::string JoinWarnings(const std::vector<std::string>& warnings) {
	std::string message;
	for (const auto& warning : warnings) {
		if (!message.empty()) message += "\n";
		message += warning;
	}
	return message;
}

bool HasIgnoredExtensionWarning(const std::vector<std::string>& warnings) {
	return std::any_of(warnings.begin(), warnings.end(), [](const std::string& warning) {
		return warning.find("extension will be ignored:") != std::string::npos;
	});
}

std::filesystem::path CanonicalScenePath(const std::filesystem::path& path) {
	std::error_code ec;
	auto canonical = std::filesystem::weakly_canonical(path, ec);
	return ec ? path.lexically_normal() : canonical;
}
}

void Editor::OnInit() {
	if (!m_UiIntegration.Initialize(GetWindow(), GetRenderer())) {
		m_FileError = "Editor UI initialization failed.";
	}
	std::string projectError;
	if (!GetAssetManager()->SetProjectRoot(EditorPlatform::LoadRememberedProjectRoot(), projectError)) {
		m_FileError = std::move(projectError);
	} else {
		EditorPlatform::RememberProjectRoot(GetAssetManager()->GetProjectRoot());
		std::vector<std::string> registryWarnings;
		if (!GetAssetManager()->RefreshRegistry(registryWarnings, projectError)) m_FileError = std::move(projectError);
		else if (!registryWarnings.empty()) m_FileError = JoinWarnings(registryWarnings);
	}

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
	m_Windows.push_back(std::make_unique<AssetBrowserPanel>());

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
	AssetRegistryChangedEvent initialAssets(GetAssetManager()->GetKnownModels(), GetAssetManager()->GetKnownTextures());
	BroadcastEvent(initialAssets);
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
	m_SceneDirty = false;
	if (const auto camera = GetActiveCamera()) {
		m_CameraFov = camera->GetFovRadians();
		m_CameraNearPlane = camera->GetNearPlane();
		m_CameraFarPlane = camera->GetFarPlane();
	}
	BroadcastSceneChanged();
	AssetRegistryChangedEvent assetUpdate(GetAssetManager()->GetKnownModels(), GetAssetManager()->GetKnownTextures());
	BroadcastEvent(assetUpdate);
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
	return m_UiIntegration.GetTextureId(m_EditorViewportTarget.get());
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
	if (!m_UiIntegration.IsInitialized()) return;
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
		if (keyEvent.Key == Engine::Events::KeyCode::N) {
			RequestSceneAction(SceneFileActionRequestedEvent::Action::NewScene);
			return true;
		}
		if (keyEvent.Key == Engine::Events::KeyCode::O) {
			RequestSceneAction(SceneFileActionRequestedEvent::Action::OpenScene);
			return true;
		}
		if (keyEvent.Key == Engine::Events::KeyCode::S) {
			RequestSceneAction((keyEvent.Mods & 0x0001) != 0
				? SceneFileActionRequestedEvent::Action::SaveSceneAs
				: SceneFileActionRequestedEvent::Action::SaveScene);
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
		const auto selectionBefore = CaptureSelection();
		m_CommandHistory.Execute(std::make_unique<EntityMembershipCommand>(m_ActiveScene, createdEntity,
			Engine::InvalidEntityId, true,
			[this, selectionBefore](auto id, const auto& entity, bool present, bool undo) {
				if (undo) { m_SceneDirty = true; RestoreSelection(selectionBefore); }
				else OnEntityMembershipChanged(id, entity, present);
			}));
		return false;
		});

	dispatcher.Dispatch<MaterialChangedEvent>([this](const auto& e) {
		auto entity = m_ActiveScene ? m_ActiveScene->FindEntity(e.GetEntityId()) : nullptr;
		if (!entity) return false;
		auto material = entity->material;
		if (entity->ModelGuid && e.GetMaterialSlot() >= 0) {
			const auto found = std::find_if(entity->Submeshes.begin(), entity->Submeshes.end(), [&](const auto& submesh) {
				return submesh.MaterialSlot == e.GetMaterialSlot();
			});
			material = found == entity->Submeshes.end() ? nullptr : found->material;
		}
		if (!material) return false;
		material->SetProperties(e.GetProperties());
		m_SceneDirty = true;
		return false;
		});

	dispatcher.Dispatch<MaterialTextureAssignmentRequestedEvent>([this](const auto& e) {
		AssignTexture(e.GetEntityId(), e.GetMaterialSlot(), e.GetSemantic(), e.GetTextureGuid());
		return true;
		});

	dispatcher.Dispatch<SceneModifiedEvent>([this](const auto&) {
		m_SceneDirty = true;
		return false;
		});

	dispatcher.Dispatch<CameraSettingsChangedEvent>([this](const auto& e) {
		m_CameraFov = e.GetFov();
		m_CameraNearPlane = e.GetNearPlane();
		m_CameraFarPlane = e.GetFarPlane();
		UpdateProjection();
		m_SceneDirty = true;
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
			m_SceneDirty = true;
		}
		return false;
		});

	dispatcher.Dispatch<SceneFileActionRequestedEvent>([this](const auto& e) {
		RequestSceneAction(e.GetAction());
		return true;
		});

	dispatcher.Dispatch<AssetImportRequestedEvent>([this](const auto& request) {
		const auto path = request.GetAssetType() == AssetImportRequestedEvent::AssetType::Texture
			? EditorPlatform::ChooseTextureFile(GetAssetManager()->GetProjectRoot())
			: EditorPlatform::ChooseModelFile(GetAssetManager()->GetProjectRoot());
		if (!path) return true;
		std::string guid;
		const bool imported = request.GetAssetType() == AssetImportRequestedEvent::AssetType::Texture
			? GetAssetManager()->ImportTexture(*path, guid, m_FileError)
			: GetAssetManager()->ImportModel(*path, guid, m_FileError);
		if (!imported) return true;
		AssetRegistryChangedEvent assetUpdate(GetAssetManager()->GetKnownModels(), GetAssetManager()->GetKnownTextures());
		BroadcastEvent(assetUpdate);
		if (request.GetTargetEntity() != Engine::InvalidEntityId) {
			const auto textureGuid = request.GetAssetType() == AssetImportRequestedEvent::AssetType::Texture
				? std::optional<std::string>(guid) : std::nullopt;
			if (!textureGuid || !AssignTexture(request.GetTargetEntity(), request.GetMaterialSlot(), request.GetSemantic(), textureGuid))
				return true;
		}
		return true;
		});

	dispatcher.Dispatch<AssetReimportRequestedEvent>([this](const auto& request) {
		ReimportAsset(request.GetGuid());
		return true;
		});

	dispatcher.Dispatch<ModelReassignmentRequestedEvent>([this](const auto& request) {
		ReassignModel(m_SelectedEntityId, request.GetGuid());
		return true;
		});

	dispatcher.Dispatch<AddModelRequestedEvent>([this](const auto& request) {
		if (!m_ActiveScene || !GetRenderSystem()) return true;
		m_FileError.clear();
		std::string error;
		if (!GetAssetManager()->LoadModel(request.GetGuid(), error)) {
			m_FileError = std::move(error);
			return true;
		}
		const auto model = GetAssetManager()->GetModel(request.GetGuid());
		const auto generation = GetAssetManager()->GetGeneration(request.GetGuid());
		if (!model || !generation) {
			m_FileError = "The imported model is not available in the asset registry.";
			return true;
		}
		if (HasIgnoredExtensionWarning(model->importWarnings)) {
			std::vector<std::string> extensionWarnings;
			std::copy_if(model->importWarnings.begin(), model->importWarnings.end(),
				std::back_inserter(extensionWarnings), [](const std::string& warning) {
					return warning.find("extension will be ignored:") != std::string::npos;
				});
			m_PendingUnsupportedModelLoad = PendingUnsupportedModelLoad{
				request.GetGuid(), request.GetPosition(), request.GetScale() };
			m_UnsupportedModelWarnings = JoinWarnings(extensionWarnings);
			return true;
		}
		AddModelToScene(request.GetGuid(), request.GetPosition(), request.GetScale());
		return true;
		});

	dispatcher.Dispatch<RemoveEntityRequestedEvent>([this](const auto& request) {
		if (!m_ActiveScene) return true;
		const auto entity = m_ActiveScene->FindEntity(request.GetId());
		if (!entity) return true;
		const auto selectionBefore = CaptureSelection();
		m_CommandHistory.Execute(std::make_unique<EntityMembershipCommand>(m_ActiveScene, entity,
			request.GetId(), false,
			[this, selectionBefore](auto id, const auto& removedEntity, bool present, bool undo) {
				if (undo) { m_SceneDirty = true; RestoreSelection(selectionBefore); }
				else OnEntityMembershipChanged(id, removedEntity, present);
			}));
		return true;
		});
}

void Editor::AddModelToScene(const std::string& guid, const DirectX::XMFLOAT3& requestedPosition,
	const DirectX::XMFLOAT3& scale) {
	if (!m_ActiveScene || !GetRenderSystem()) return;
	std::string error;
	if (!GetAssetManager()->LoadModel(guid, error)) {
		m_FileError = std::move(error);
		return;
	}
	const auto model = GetAssetManager()->GetModel(guid);
	const auto generation = GetAssetManager()->GetGeneration(guid);
	if (!model || !generation) {
		m_FileError = "The imported model is not available in the asset registry.";
		return;
	}
	std::vector<std::string> warnings;
	auto entity = GetRenderSystem()->CreateModelEntity(guid, *generation, *model, {}, error, warnings);
	if (!entity) {
		m_FileError = std::move(error);
		return;
	}
	entity->Name = model->sourcePath.stem().string();
	const auto position = m_ActiveScene->FindFreeSpawnPosition(requestedPosition);
	entity->transform.SetPosition(position.x, position.y, position.z);
	entity->transform.SetScale(scale.x, scale.y, scale.z);
	const auto selectionBefore = CaptureSelection();
	m_CommandHistory.Execute(std::make_unique<EntityMembershipCommand>(m_ActiveScene, entity,
		Engine::InvalidEntityId, true,
		[this, selectionBefore](auto id, const auto& addedEntity, bool present, bool undo) {
			if (undo) { m_SceneDirty = true; RestoreSelection(selectionBefore); }
			else OnEntityMembershipChanged(id, addedEntity, present);
		}));
	if (!warnings.empty()) m_FileError = JoinWarnings(warnings);
}

void Editor::OnEntityMembershipChanged(
	Engine::EntityId id,
	const std::shared_ptr<Renderer::Entity>& entity,
	bool present) {
	m_SceneDirty = true;
	if (present) {
		m_SelectedItemType = SelectedItemType::Object;
		m_SelectedEntityId = id;
		m_SelectedLightId = Engine::InvalidLightId;
		m_SelectedCameraId = Engine::InvalidCameraId;
		BroadcastSceneChanged();
		EntitySelectedEvent selected(EntitySelectedEvent::ItemType::Object,
			static_cast<int>(m_ActiveScene->GetEntityCount() - 1), -1, -1, id,
			Engine::InvalidLightId, Engine::InvalidCameraId, entity, {}, {});
		BroadcastEvent(selected);
		return;
	}
	BroadcastSceneChanged();
	if (m_SelectedItemType == SelectedItemType::Object && m_SelectedEntityId == id) {
		m_SelectedItemType = SelectedItemType::None;
		m_SelectedEntityId = Engine::InvalidEntityId;
		m_SelectedLightId = Engine::InvalidLightId;
		m_SelectedCameraId = Engine::InvalidCameraId;
		EntityDeselectedEvent deselected;
		BroadcastEvent(deselected);
	}
}

Editor::SelectionSnapshot Editor::CaptureSelection() const noexcept {
	return { m_SelectedItemType, m_SelectedEntityId, m_SelectedLightId, m_SelectedCameraId };
}

void Editor::RestoreSelection(const SelectionSnapshot& snapshot) {
	if (!m_ActiveScene) return;
	if (snapshot.Type == SelectedItemType::Object && m_ActiveScene->FindEntity(snapshot.EntityId)) {
		int index = -1;
		for (size_t i = 0; i < m_ActiveScene->GetEntityCount(); ++i)
			if (m_ActiveScene->GetEntityIdAt(i) == snapshot.EntityId) { index = static_cast<int>(i); break; }
		ProcessPanelEvent(std::make_unique<EntitySelectedEvent>(EntitySelectedEvent::ItemType::Object,
			index, -1, -1, snapshot.EntityId, Engine::InvalidLightId, Engine::InvalidCameraId));
		return;
	}
	if (snapshot.Type == SelectedItemType::Light && m_ActiveScene->FindLight(snapshot.LightId)) {
		ProcessPanelEvent(std::make_unique<EntitySelectedEvent>(EntitySelectedEvent::ItemType::Light,
			-1, -1, -1, Engine::InvalidEntityId, snapshot.LightId, Engine::InvalidCameraId));
		return;
	}
	if (snapshot.Type == SelectedItemType::Camera && m_ActiveScene->FindCamera(snapshot.CameraId)) {
		ProcessPanelEvent(std::make_unique<EntitySelectedEvent>(EntitySelectedEvent::ItemType::Camera,
			-1, -1, -1, Engine::InvalidEntityId, Engine::InvalidLightId, snapshot.CameraId));
		return;
	}
	ProcessPanelEvent(std::make_unique<EntityDeselectedEvent>());
}

bool Editor::AssignTexture(Engine::EntityId entityId, int32_t materialSlot,
	AssetImportRequestedEvent::TextureSemantic semantic, const std::optional<std::string>& textureGuid) {
	auto entity = m_ActiveScene ? m_ActiveScene->FindEntity(entityId) : nullptr;
	if (!entity || !GetRenderSystem()) return false;
	auto material = entity->material;
	if (entity->ModelGuid && materialSlot >= 0) {
		const auto found = std::find_if(entity->Submeshes.begin(), entity->Submeshes.end(), [materialSlot](const auto& submesh) {
			return submesh.MaterialSlot == materialSlot;
		});
		material = found == entity->Submeshes.end() ? nullptr : found->material;
	}
	if (!material) { m_FileError = "The selected material slot is no longer available."; return false; }
	auto baseColor = material->GetBaseColorTexture();
	auto normal = material->GetNormalTexture();
	auto baseGuid = material->GetBaseColorTextureGuid();
	auto normalGuid = material->GetNormalTextureGuid();
	const auto beforeBaseColor = baseColor;
	const auto beforeNormal = normal;
	const auto beforeBaseGuid = baseGuid;
	const auto beforeNormalGuid = normalGuid;
	if (semantic == AssetImportRequestedEvent::TextureSemantic::BaseColor) {
		baseGuid = textureGuid;
		if (textureGuid) {
			std::string error;
			if (!GetAssetManager()->LoadTexture(*textureGuid, error)) { m_FileError = std::move(error); return false; }
			const auto image = GetAssetManager()->GetTexture(*textureGuid);
			const auto generation = GetAssetManager()->GetGeneration(*textureGuid);
			if (!image || !generation) { m_FileError = "The selected texture has no decoded image data."; return false; }
			baseColor = GetRenderSystem()->GetOrCreateTexture(*textureGuid, *generation, *image, true, error);
			if (!baseColor) { m_FileError = std::move(error); return false; }
		} else baseColor = material->GetDefaultBaseColorTexture();
	} else {
		normalGuid = textureGuid;
		if (textureGuid) {
			std::string error;
			if (!GetAssetManager()->LoadTexture(*textureGuid, error)) { m_FileError = std::move(error); return false; }
			const auto image = GetAssetManager()->GetTexture(*textureGuid);
			const auto generation = GetAssetManager()->GetGeneration(*textureGuid);
			if (!image || !generation) { m_FileError = "The selected texture has no decoded image data."; return false; }
			normal = GetRenderSystem()->GetOrCreateTexture(*textureGuid, *generation, *image, false, error);
			if (!normal) { m_FileError = std::move(error); return false; }
		} else normal = material->GetDefaultNormalTexture();
	}
	if (baseGuid == beforeBaseGuid && normalGuid == beforeNormalGuid) return true;
	m_CommandHistory.Execute(std::make_unique<ChangeMaterialTextureCommand>(material,
		beforeBaseColor, beforeNormal, beforeBaseGuid, beforeNormalGuid, std::move(baseColor), std::move(normal),
		baseGuid, normalGuid,
		[this](const auto& target, const auto& base, const auto& normalMap, const auto& baseId, const auto& normalId) {
			if (!GetRenderSystem()->SetMaterialTextures(target, base, normalMap, baseId, normalId)) {
				m_FileError = "The renderer could not update the material texture bindings.";
				return false;
			}
			m_FileError.clear();
			m_SceneDirty = true;
			return true;
		}));
	return true;
}

void Editor::ReassignModel(Engine::EntityId entityId, const std::string& guid) {
	if (!m_ActiveScene || !GetRenderSystem()) return;
	auto before = m_ActiveScene->FindEntity(entityId);
	if (!before) { m_FileError = "Select a scene object before replacing its model."; return; }
	std::string error;
	if (!GetAssetManager()->LoadModel(guid, error)) { m_FileError = std::move(error); return; }
	const auto model = GetAssetManager()->GetModel(guid);
	const auto generation = GetAssetManager()->GetGeneration(guid);
	if (!model || !generation) { m_FileError = "The selected model is unavailable in the asset registry."; return; }
	std::vector<std::optional<Renderer::MaterialProperties>> overrides(model->materials.size());
	std::unordered_map<int32_t, std::shared_ptr<Renderer::Material>> oldSlots;
	for (const auto& submesh : before->Submeshes) {
		if (submesh.MaterialSlot >= 0 && submesh.material) oldSlots.try_emplace(submesh.MaterialSlot, submesh.material);
	}
	std::shared_ptr<Renderer::Material> primitiveMaterial = before->ModelGuid ? nullptr : before->material;
	for (size_t slot = 0; slot < overrides.size(); ++slot) {
		const auto found = oldSlots.find(static_cast<int32_t>(slot));
		if (found != oldSlots.end()) overrides[slot] = found->second->GetProperties();
		else if (primitiveMaterial && slot == 0) overrides[slot] = primitiveMaterial->GetProperties();
	}
	std::vector<std::string> warnings;
	auto after = GetRenderSystem()->CreateModelEntity(guid, *generation, *model, overrides, error, warnings);
	if (!after) { m_FileError = std::move(error); return; }
	after->Name = before->Name;
	after->transform.SetPosition(before->transform.Position.x, before->transform.Position.y, before->transform.Position.z);
	after->transform.Rotation = before->transform.Rotation;
	after->transform.SetScale(before->transform.Scale.x, before->transform.Scale.y, before->transform.Scale.z);
	after->ForceWireframe = before->ForceWireframe;
	after->DisableDepthTest = before->DisableDepthTest;
	after->CastShadows = before->CastShadows;
	after->ReceiveShadows = before->ReceiveShadows;
	after->CullMode = before->CullMode;
	std::unordered_set<int32_t> appliedSlots;
	for (const auto& submesh : after->Submeshes) {
		if (submesh.MaterialSlot < 0 || !submesh.material || !appliedSlots.insert(submesh.MaterialSlot).second) continue;
		std::shared_ptr<Renderer::Material> sourceMaterial;
		if (const auto found = oldSlots.find(submesh.MaterialSlot); found != oldSlots.end()) sourceMaterial = found->second;
		else if (primitiveMaterial && submesh.MaterialSlot == 0) sourceMaterial = primitiveMaterial;
		if (!sourceMaterial) continue;
		auto base = submesh.material->GetBaseColorTexture();
		auto normal = submesh.material->GetNormalTexture();
		const auto rebind = [&](const std::optional<std::string>& textureGuid, bool srgb,
			std::shared_ptr<Renderer::TextureResource>& destination) {
			if (!textureGuid) return;
			std::string textureError;
			if (!GetAssetManager()->LoadTexture(*textureGuid, textureError)) { warnings.push_back(textureError); return; }
			const auto image = GetAssetManager()->GetTexture(*textureGuid);
			const auto textureGeneration = GetAssetManager()->GetGeneration(*textureGuid);
			if (!image || !textureGeneration) { warnings.push_back("missing texture " + *textureGuid); return; }
			auto uploaded = GetRenderSystem()->GetOrCreateTexture(*textureGuid, *textureGeneration, *image, srgb, textureError);
			if (uploaded) destination = std::move(uploaded);
			else warnings.push_back(textureError);
		};
		rebind(sourceMaterial->GetBaseColorTextureGuid(), true, base);
		rebind(sourceMaterial->GetNormalTextureGuid(), false, normal);
		if (!GetRenderSystem()->SetMaterialTextures(submesh.material, base, normal,
			sourceMaterial->GetBaseColorTextureGuid(), sourceMaterial->GetNormalTextureGuid())) {
			for (const auto& staged : after->Submeshes)
				if (staged.material) GetRenderSystem()->GetMaterialSystem()->UnregisterMaterial(staged.material);
			m_FileError = "The new model's texture bindings could not be created.";
			return;
		}
	}
	m_CommandHistory.Execute(std::make_unique<ReplaceEntityCommand>(m_ActiveScene, entityId, before, after,
		[this](Engine::EntityId id, const std::shared_ptr<Renderer::Entity>& entity) {
			m_SceneDirty = true;
			BroadcastSceneChanged();
			int index = -1;
			for (size_t i = 0; i < m_ActiveScene->GetEntityCount(); ++i)
				if (m_ActiveScene->GetEntityIdAt(i) == id) { index = static_cast<int>(i); break; }
			ProcessPanelEvent(std::make_unique<EntitySelectedEvent>(EntitySelectedEvent::ItemType::Object,
				index, -1, -1, id, Engine::InvalidLightId, Engine::InvalidCameraId, entity,
				std::weak_ptr<Renderer::SpotLight>{}, std::weak_ptr<Renderer::Camera>{}));
		}));
	m_FileError = JoinWarnings(warnings);
}

void Editor::ReimportAsset(const std::string& guid) {
	const auto sourcePath = GetAssetManager()->GetSourcePath(guid);
	if (!sourcePath) { m_FileError = "The selected asset is no longer in the project registry."; return; }
	std::string error, importedGuid;
	if (GetAssetManager()->GetTexture(guid)) {
		if (!m_ActiveScene) { m_FileError = "There is no active scene to refresh."; return; }
		if (!GetAssetManager()->ImportTexture(*sourcePath, importedGuid, error)) { m_FileError = std::move(error); return; }
		const auto image = GetAssetManager()->GetTexture(guid);
		const auto generation = GetAssetManager()->GetGeneration(guid);
		if (!image || !generation) { m_FileError = "Texture reimport completed without decoded image data."; return; }
		std::unordered_set<Renderer::Material*> updated;
		for (const auto& entity : m_ActiveScene->GetEntities()) {
			if (!entity) continue;
			std::vector<std::shared_ptr<Renderer::Material>> materials;
			if (entity->material) materials.push_back(entity->material);
			for (const auto& submesh : entity->Submeshes) if (submesh.material) materials.push_back(submesh.material);
			for (const auto& material : materials) {
				if (!updated.insert(material.get()).second) continue;
				const bool baseMatch = material->GetBaseColorTextureGuid() == std::optional<std::string>(guid);
				const bool normalMatch = material->GetNormalTextureGuid() == std::optional<std::string>(guid);
				if (!baseMatch && !normalMatch) continue;
				auto base = material->GetBaseColorTexture();
				auto normal = material->GetNormalTexture();
				if (baseMatch) base = GetRenderSystem()->GetOrCreateTexture(guid, *generation, *image, true, error);
				if (normalMatch) normal = GetRenderSystem()->GetOrCreateTexture(guid, *generation, *image, false, error);
				if (!base || !normal || !GetRenderSystem()->SetMaterialTextures(material, base, normal,
					material->GetBaseColorTextureGuid(), material->GetNormalTextureGuid())) {
					m_FileError = error.empty() ? "Texture reimport could not refresh all live material bindings." : error;
					return;
				}
			}
		}
		AssetRegistryChangedEvent update(GetAssetManager()->GetKnownModels(), GetAssetManager()->GetKnownTextures());
		BroadcastEvent(update);
		m_FileError.clear();
		return;
	}

	if (!GetAssetManager()->ImportModel(*sourcePath, importedGuid, error)) { m_FileError = std::move(error); return; }
	if (!m_ActiveScene) return;
	const auto model = GetAssetManager()->GetModel(guid);
	const auto generation = GetAssetManager()->GetGeneration(guid);
	if (!model || !generation) { m_FileError = "Model reimport completed without an imported model."; return; }
	struct Replacement { Engine::EntityId id; std::shared_ptr<Renderer::Entity> entity; };
	std::vector<Replacement> replacements;
	std::vector<std::string> warnings;
	const auto cleanup = [&]() {
		for (const auto& replacement : replacements) {
			for (const auto& submesh : replacement.entity->Submeshes)
				if (submesh.material) GetRenderSystem()->GetMaterialSystem()->UnregisterMaterial(submesh.material);
		}
	};
	for (size_t i = 0; i < m_ActiveScene->GetEntities().size(); ++i) {
		const auto oldEntity = m_ActiveScene->GetEntities()[i];
		if (!oldEntity || oldEntity->ModelGuid != std::optional<std::string>(guid)) continue;
		std::vector<std::optional<Renderer::MaterialProperties>> overrides(model->materials.size());
		for (const auto& submesh : oldEntity->Submeshes) {
			if (submesh.MaterialSlot >= 0 && static_cast<size_t>(submesh.MaterialSlot) < overrides.size() && submesh.material)
				overrides[static_cast<size_t>(submesh.MaterialSlot)] = submesh.material->GetProperties();
		}
		auto replacement = GetRenderSystem()->CreateModelEntity(guid, *generation, *model, overrides, error, warnings);
		if (!replacement) { cleanup(); m_FileError = std::move(error); return; }
		replacement->Name = oldEntity->Name;
		replacement->transform.SetPosition(oldEntity->transform.Position.x, oldEntity->transform.Position.y, oldEntity->transform.Position.z);
		replacement->transform.Rotation = oldEntity->transform.Rotation;
		replacement->transform.SetScale(oldEntity->transform.Scale.x, oldEntity->transform.Scale.y, oldEntity->transform.Scale.z);
		replacement->ForceWireframe = oldEntity->ForceWireframe;
		replacement->DisableDepthTest = oldEntity->DisableDepthTest;
		replacement->CastShadows = oldEntity->CastShadows;
		replacement->ReceiveShadows = oldEntity->ReceiveShadows;
		replacement->CullMode = oldEntity->CullMode;
		std::unordered_set<int32_t> appliedSlots;
		for (const auto& oldSubmesh : oldEntity->Submeshes) {
			if (oldSubmesh.MaterialSlot < 0 || !oldSubmesh.material || !appliedSlots.insert(oldSubmesh.MaterialSlot).second) continue;
			const auto next = std::find_if(replacement->Submeshes.begin(), replacement->Submeshes.end(), [&](const auto& item) {
				return item.MaterialSlot == oldSubmesh.MaterialSlot;
			});
			if (next == replacement->Submeshes.end()) continue;
			auto base = next->material->GetBaseColorTexture();
			auto normal = next->material->GetNormalTexture();
			const auto rebind = [&](const std::optional<std::string>& textureGuid, bool srgb,
				std::shared_ptr<Renderer::TextureResource>& destination) {
				if (!textureGuid) return;
				std::string textureError;
				if (!GetAssetManager()->LoadTexture(*textureGuid, textureError)) { warnings.push_back(textureError); return; }
				const auto texture = GetAssetManager()->GetTexture(*textureGuid);
				const auto textureGeneration = GetAssetManager()->GetGeneration(*textureGuid);
				if (!texture || !textureGeneration) { warnings.push_back("missing texture " + *textureGuid); return; }
				auto uploaded = GetRenderSystem()->GetOrCreateTexture(*textureGuid, *textureGeneration, *texture, srgb, textureError);
				if (uploaded) destination = std::move(uploaded);
				else warnings.push_back(textureError);
			};
			rebind(oldSubmesh.material->GetBaseColorTextureGuid(), true, base);
			rebind(oldSubmesh.material->GetNormalTextureGuid(), false, normal);
			if (!GetRenderSystem()->SetMaterialTextures(next->material, base, normal,
				oldSubmesh.material->GetBaseColorTextureGuid(), oldSubmesh.material->GetNormalTextureGuid())) {
				for (const auto& submesh : replacement->Submeshes)
					if (submesh.material) GetRenderSystem()->GetMaterialSystem()->UnregisterMaterial(submesh.material);
				cleanup();
				m_FileError = "Model reimport could not restore material texture bindings.";
				return;
			}
		}
		replacements.push_back({ m_ActiveScene->GetEntityIdAt(i), std::move(replacement) });
	}
	for (auto& replacement : replacements) m_ActiveScene->ReplaceEntity(replacement.id, replacement.entity);
	BroadcastSceneChanged();
	AssetRegistryChangedEvent update(GetAssetManager()->GetKnownModels(), GetAssetManager()->GetKnownTextures());
	BroadcastEvent(update);
	if (m_SelectedEntityId != Engine::InvalidEntityId)
		ProcessPanelEvent(std::make_unique<EntitySelectedEvent>(EntitySelectedEvent::ItemType::Object, -1, -1, -1,
			m_SelectedEntityId, Engine::InvalidLightId, Engine::InvalidCameraId));
	m_FileError = JoinWarnings(warnings);
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
		m_SceneDirty = true;
	}
}

void Editor::Redo() {
	if (m_CommandHistory.CanRedo()) {
		m_CommandHistory.Redo();
		m_SceneDirty = true;
	}
}

void Editor::OnUIRender() {
	if (!m_UiIntegration.IsInitialized()) return;
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
	DrawUnsavedChangesModal();
}

void Editor::OnBeginUIFrame() {
	m_UiIntegration.BeginFrame();
}

void Editor::OnEndUIFrame() {
	m_UiIntegration.EndFrame();
}

void Editor::OnShutdownUI() {
	m_UiIntegration.Shutdown();
}

void Editor::RequestSceneAction(SceneFileActionRequestedEvent::Action action) {
	m_FileError.clear();
	if (m_SceneDirty) {
		m_PendingSceneAction = action;
		m_ShowUnsavedModal = true;
		return;
	}
	if (action == SceneFileActionRequestedEvent::Action::NewScene) {
		auto scene = std::make_shared<Engine::Scene>();
		auto camera = std::make_shared<Renderer::Camera>(m_CameraFov, m_CameraNearPlane, m_CameraFarPlane);
		camera->m_Transform.SetPosition(0.0f, 0.0f, -5.0f);
		scene->AddCamera(camera);
		if (m_ActiveScene) RemoveScene(m_ActiveScene);
		AddScene(scene);
		OnSceneReplaced(scene);
		m_ScenePath.clear();
		m_SceneDirty = false;
		return;
	}
	if (action == SceneFileActionRequestedEvent::Action::SaveScene) {
		SaveCurrentScene(m_ScenePath.empty());
		return;
	}
	if (action == SceneFileActionRequestedEvent::Action::SaveSceneAs) {
		SaveCurrentScene(true);
		return;
	}
	if (action == SceneFileActionRequestedEvent::Action::OpenProject) {
		const auto folder = EditorPlatform::ChooseProjectFolder(GetAssetManager()->GetProjectRoot());
		if (!folder) return;
		std::string error;
		if (!GetAssetManager()->SetProjectRoot(*folder, error)) {
			m_FileError = std::move(error);
			return;
		}
		std::vector<std::string> registryWarnings;
		if (!GetAssetManager()->RefreshRegistry(registryWarnings, error)) {
			m_FileError = std::move(error);
			return;
		}
		EditorPlatform::RememberProjectRoot(GetAssetManager()->GetProjectRoot());
		auto scene = std::make_shared<Engine::Scene>();
		auto camera = std::make_shared<Renderer::Camera>(m_CameraFov, m_CameraNearPlane, m_CameraFarPlane);
		camera->m_Transform.SetPosition(0.0f, 0.0f, -5.0f);
		scene->AddCamera(camera);
		if (m_ActiveScene) RemoveScene(m_ActiveScene);
		AddScene(scene);
		OnSceneReplaced(scene);
		m_ScenePath.clear();
		m_SceneDirty = false;
		if (!registryWarnings.empty()) m_FileError = JoinWarnings(registryWarnings);
		return;
	}
	const auto path = EditorPlatform::ChooseSceneFile(false, m_ScenePath, GetAssetManager()->GetProjectRoot());
	if (!path) return;
	std::string error;
	std::vector<std::string> warnings;
	if (!LoadScene(*path, error, warnings)) {
		m_FileError = std::move(error);
		return;
	}
	m_ScenePath = CanonicalScenePath(*path);
	m_SceneDirty = false;
	if (!warnings.empty()) m_FileError = JoinWarnings(warnings);
}

bool Editor::SaveCurrentScene(bool choosePath) {
	if (!m_ActiveScene) {
		m_FileError = "There is no active scene to save.";
		return false;
	}
	auto path = choosePath
		? EditorPlatform::ChooseSceneFile(true, m_ScenePath, GetAssetManager()->GetProjectRoot())
		: std::optional(m_ScenePath);
	if (!path) return false;
	std::string error;
	if (!SaveScene(*path, *m_ActiveScene, error)) {
		m_FileError = std::move(error);
		return false;
	}
	m_ScenePath = CanonicalScenePath(*path);
	m_SceneDirty = false;
	m_FileError.clear();
	return true;
}

void Editor::DrawUnsavedChangesModal() {
	if (m_ShowUnsavedModal) {
		ImGui::OpenPopup("Unsaved changes");
		m_ShowUnsavedModal = false;
	}
	if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted("Save changes to the current scene before continuing?");
		if (ImGui::Button("Save", ImVec2(120, 0))) {
			if (SaveCurrentScene(m_ScenePath.empty())) {
				const auto action = m_PendingSceneAction;
				ImGui::CloseCurrentPopup();
				RequestSceneAction(action);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Discard", ImVec2(120, 0))) {
			m_SceneDirty = false;
			const auto action = m_PendingSceneAction;
			ImGui::CloseCurrentPopup();
			RequestSceneAction(action);
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	if (!m_FileError.empty()) {
		ImGui::OpenPopup("Scene operation");
	}
	ImGui::SetNextWindowSize(ImVec2(640.0f, 320.0f), ImGuiCond_Appearing);
	if (ImGui::BeginPopupModal("Scene operation")) {
		ImGui::BeginChild("Operation details", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), true);
		ImGui::TextWrapped("%s", m_FileError.c_str());
		ImGui::EndChild();
		const float buttonWidth = ImGui::CalcTextSize("OK").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) * 0.5f);
		if (ImGui::Button("OK")) {
			m_FileError.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	if (m_PendingUnsupportedModelLoad) ImGui::OpenPopup("Unsupported model extensions");
	ImGui::SetNextWindowSize(ImVec2(640.0f, 300.0f), ImGuiCond_Appearing);
	if (ImGui::BeginPopupModal("Unsupported model extensions", nullptr, ImGuiWindowFlags_NoResize)) {
		ImGui::TextWrapped("This model uses a glTF extension that the renderer does not support. Its geometry can still be loaded, but the affected material may look different.");
		ImGui::Spacing();
		ImGui::BeginChild("Unsupported extension details", ImVec2(0.0f, 150.0f), true);
		ImGui::TextWrapped("%s", m_UnsupportedModelWarnings.c_str());
		ImGui::EndChild();
		if (ImGui::Button("Cancel", ImVec2(140.0f, 0.0f))) {
			m_PendingUnsupportedModelLoad.reset();
			m_UnsupportedModelWarnings.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Load unsupported", ImVec2(180.0f, 0.0f))) {
			auto pending = std::move(*m_PendingUnsupportedModelLoad);
			m_PendingUnsupportedModelLoad.reset();
			m_UnsupportedModelWarnings.clear();
			ImGui::CloseCurrentPopup();
			AddModelToScene(pending.Guid, pending.Position, pending.Scale);
		}
		ImGui::EndPopup();
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
			ImGui::DockBuilderDockWindow("Assets", dockBottom);
	ImGui::DockBuilderDockWindow("Viewport", dockMain);
	ImGui::DockBuilderFinish(dockspaceId);
}

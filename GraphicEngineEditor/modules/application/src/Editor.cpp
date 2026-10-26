#include "application/include/Editor.h"

#include "application/include/EditorProjectWorkflow.h"
#include "events/include/AssetRegistryChangedEvent.h"
#include "events/include/EcsComponentMutationRequestedEvent.h"
#include "events/include/EcsEntityCreateRequestedEvent.h"
#include "events/include/EcsEntityDeleteRequestedEvent.h"
#include "events/include/EcsEntityReparentRequestedEvent.h"
#include "events/include/EcsEntitySelectedEvent.h"
#include "events/include/EditorEvents.h"
#include "panels/include/AssetBrowserPanel.h"
#include "panels/include/AssetDropAssignment.h"
#include "panels/include/CameraPreviewPanel.h"
#include "panels/include/CameraPreviewRequest.h"
#include "panels/include/EngineSettingsPanel.h"
#include "panels/include/HierarchyPanel.h"
#include "panels/include/InspectorPanel.h"
#include "panels/include/MainBar.h"
#include "panels/include/ScenePanel.h"
#include "panels/include/StatisticsPanel.h"
#include "platform/include/EditorFileDialogs.h"
#include "rendering/include/RenderSystem.h"
#include "rendering/include/ViewportTarget.h"
#include "rendering/include/renderer/CameraView.h"
#include "rendering/include/renderer/Viewport.h"
#include "runtime/include/LogMacros.h"
#include "runtime/include/Window.h"
#include "scene/include/Scene.h"
#include "scene/include/components/EcsComponents.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>
#include <span>
#include <string>
#include <tuple>
#include <utility>

namespace {
auto QuaternionToPitchYawRoll(const std::array<float, 4>& quaternion) -> std::array<float, 3> {
  const DirectX::XMVECTOR rotation =
      DirectX::XMQuaternionNormalize(DirectX::XMVectorSet(quaternion[0], quaternion[1], quaternion[2], quaternion[3]));
  const DirectX::XMVECTOR forward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0.0F, 0.0F, 1.0F, 0.0F), rotation);
  const float pitch = std::asin(std::clamp(-DirectX::XMVectorGetY(forward), -1.0F, 1.0F));
  const float yaw = std::atan2(DirectX::XMVectorGetX(forward), DirectX::XMVectorGetZ(forward));
  const DirectX::XMVECTOR noRollRotation = DirectX::XMQuaternionRotationRollPitchYaw(pitch, yaw, 0.0F);
  const DirectX::XMVECTOR baseUp =
      DirectX::XMVector3Rotate(DirectX::XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F), noRollRotation);
  const DirectX::XMVECTOR baseRight =
      DirectX::XMVector3Rotate(DirectX::XMVectorSet(1.0F, 0.0F, 0.0F, 0.0F), noRollRotation);
  const DirectX::XMVECTOR actualUp = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F), rotation);
  const float roll = std::atan2(DirectX::XMVectorGetX(DirectX::XMVector3Dot(actualUp, baseRight)),
                                DirectX::XMVectorGetX(DirectX::XMVector3Dot(actualUp, baseUp)));
  return {pitch, yaw, roll};
}
} // namespace

void Editor::OnInit() {
  m_EditorPreferences = EditorPlatform::LoadEditorPreferences();
  SetFrameLimiterHz(m_EditorPreferences.FrameLimiterHz);
  if (Engine::RenderSystem* renderSystem = GetRenderSystem()) {
    if (!renderSystem->SetMaxTextureAnisotropy(m_EditorPreferences.MaxTextureAnisotropy)) {
      ENGINE_LOG_WARN("Render", "Saved texture anisotropy setting was rejected; using renderer defaults");
    }
    Engine::ShadowSettings shadowSettings;
    shadowSettings.Resolution = m_EditorPreferences.ShadowMapResolution;
    shadowSettings.MaxShadowCastersPerScene = m_EditorPreferences.MaxShadowCasters;
    if (!renderSystem->SetShadowSettings(shadowSettings)) {
      ENGINE_LOG_WARN("Render", "Saved shadow settings were rejected; using renderer defaults");
    }
  }
  if (Engine::IRenderBackend* backend = GetRenderer(); backend != nullptr && backend->SupportsVSyncControl()) {
    (void)backend->SetVSyncEnabled(m_EditorPreferences.VSyncEnabled);
  }
  m_EditorConsole.AttachToLogger();
  m_RenderOutputExportService = std::make_unique<Engine::RenderOutputExportService>();
  if (!m_UiIntegration.Initialize(GetWindow(), GetRenderer())) {
    ENGINE_LOG_ERROR("Core", "Editor UI initialization failed");
  }

  std::string projectError;
  if (!GetAssetManager()->SetProjectRoot(EditorPlatform::LoadRememberedProjectRoot(), projectError)) {
    ENGINE_LOG_WARN("Asset", "Project root initialization failed | reason: {}", projectError);
  } else {
    EditorPlatform::RememberProjectRoot(GetAssetManager()->GetProjectRoot());
    std::vector<std::string> warnings;
    if (!GetAssetManager()->RefreshRegistry(warnings, projectError)) {
      ENGINE_LOG_WARN("Asset", "Asset registry refresh failed | reason: {}", projectError);
    }
  }

  m_ActiveScene = std::make_shared<Engine::Scene>();
  const uint64_t meshCreate = m_ActiveScene->QueueCreate("Cube");
  const uint64_t lightCreate = m_ActiveScene->QueueCreate("Spot Light");
  Engine::EntityHandle meshEntity = Engine::InvalidEntityHandle;
  Engine::EntityHandle lightEntity = Engine::InvalidEntityHandle;
  for (const auto& result : m_ActiveScene->sync()) {
    if (!result.Outcome) {
      ENGINE_LOG_ERROR("Scene", "Default scene entity creation failed | reason: {}", result.Outcome.error().Message);
      continue;
    }
    if (result.Sequence == meshCreate) {
      meshEntity = result.Entity;
    } else if (result.Sequence == lightCreate) {
      lightEntity = result.Entity;
    }
  }

  if (meshEntity != Engine::InvalidEntityHandle) {
    Engine::Ecs::MeshRenderer meshRenderer;
    meshRenderer.PrimitiveType = "cube";
    (void)m_ActiveScene->QueueAdd(meshEntity, std::move(meshRenderer));
  }
  if (lightEntity != Engine::InvalidEntityHandle) {
    Engine::Ecs::LocalTransform lightTransform;
    lightTransform.Position = {2.0F, 3.0F, -1.0F};
    Engine::Ecs::SpotLight light;
    light.Direction = {-0.4F, -0.7F, 0.2F};
    light.Intensity = 8.0F;
    light.Type = Engine::Ecs::LightType::Spot;
    (void)m_ActiveScene->QueueSet(lightEntity, lightTransform);
    (void)m_ActiveScene->QueueAdd(lightEntity, light);
  }
  for (const auto& result : m_ActiveScene->sync()) {
    if (!result.Outcome) {
      ENGINE_LOG_ERROR("Scene", "Default scene component setup failed | reason: {}", result.Outcome.error().Message);
    }
  }
  m_SelectedEntity = meshEntity;
  AddScene(m_ActiveScene);

  if (Engine::RenderSystem* renderSystem = GetRenderSystem()) {
    auto material = renderSystem->CreateMaterial({});
    if (material) {
      constexpr std::array primitiveShapes = {Engine::PrimitiveShape::Cube, Engine::PrimitiveShape::Sphere,
                                              Engine::PrimitiveShape::Plane, Engine::PrimitiveShape::Cone};
      for (const Engine::PrimitiveShape shape : primitiveShapes) {
        if (!renderSystem->CreatePrimitive(shape, material)) {
          ENGINE_LOG_WARN("Render", "Editor primitive resource setup failed | shape: {}", static_cast<uint32_t>(shape));
        }
      }
    }
  }

  CreateViewport(1280, 720);
  auto mainBar = std::make_unique<MainBar>();
  mainBar->SetConsoleOpenHandler([this] -> void { m_EngineConsoleOpen = true; });
  auto engineSettings = std::make_unique<EngineSettingsPanel>(
      this, [this] { return m_EditorPreferences; },
      [this](const auto& preferences) {
        m_EditorPreferences = preferences;
        EditorPlatform::SaveEditorPreferences(m_EditorPreferences);
      });
  auto* engineSettingsPanel = engineSettings.get();
  mainBar->SetSettingsOpenHandler([engineSettingsPanel] -> void { engineSettingsPanel->Open(); });
  m_Panels.push_back(std::move(mainBar));
  m_Panels.push_back(std::move(engineSettings));
  m_Panels.push_back(std::make_unique<StatWindow>(this));
  m_Panels.push_back(std::make_unique<ScenePanel>());
  m_Panels.push_back(std::make_unique<HierarchyPanel>());
  m_Panels.push_back(std::make_unique<InspectorPanel>());
  m_Panels.push_back(std::make_unique<AssetBrowserPanel>());
  auto cameraPreviewPanel = std::make_unique<CameraPreviewPanel>(
      [this](Engine::EntityHandle entity) -> Engine::Result<Engine::RenderOutput> {
        if (!m_ActiveScene || GetRenderSystem() == nullptr) {
          return std::unexpected(Engine::MakeError("No active scene or render system is available"));
        }
        const auto request = ResolveCameraPreviewRequest(m_ActiveScene->World(), entity);
        if (!request) {
          return std::unexpected(request.error());
        }
        return GetRenderSystem()->RenderFinal(*m_ActiveScene, request->View, request->Width, request->Height, {}, {},
                                              request->PostProcessing, request->AmbientLightOverride);
      },
      [this](uint32_t width, uint32_t height, std::span<const uint8_t> pixels) -> bool {
        return m_UiIntegration.SetCameraPreview(width, height, pixels);
      },
      [this]() -> void* { return m_UiIntegration.GetCameraPreviewTextureId(); },
      [this]() -> void { m_UiIntegration.ClearCameraPreview(); },
      [](bool exr, const std::filesystem::path& initialPath) {
        return EditorPlatform::ChooseRenderOutputFile(exr, initialPath);
      },
      [this](Engine::EntityHandle entity, bool exr, std::vector<Engine::RenderAov> selectedAovs,
             std::filesystem::path destination, Engine::RenderOutputBackground background)
          -> Engine::Result<Engine::RenderOutputExportService::RequestId> {
        Engine::RenderSystem* renderer = GetRenderSystem();
        if (!m_ActiveScene || renderer == nullptr || m_RenderOutputExportService == nullptr) {
          return std::unexpected(Engine::MakeError("No active scene or render system is available"));
        }
        const auto request = ResolveCameraPreviewRequest(m_ActiveScene->World(), entity);
        if (!request) {
          return std::unexpected(request.error());
        }
        if (exr) {
          return m_RenderOutputExportService->StartMultilayerExr(
              *renderer, *m_ActiveScene, request->View, request->Width, request->Height, std::move(selectedAovs),
              std::move(destination), {}, background, request->PostProcessing, request->AmbientLightOverride);
        }
        return m_RenderOutputExportService->StartPng(*renderer, *m_ActiveScene, request->View, request->Width,
                                                     request->Height, std::move(destination), {}, background,
                                                     request->PostProcessing, request->AmbientLightOverride);
      },
      [this](
          Engine::RenderOutputExportService::RequestId requestId) -> std::optional<Engine::RenderOutputExportStatus> {
        return m_RenderOutputExportService != nullptr ? m_RenderOutputExportService->GetStatus(requestId)
                                                      : std::nullopt;
      });
  m_CameraPreviewPanel = cameraPreviewPanel.get();
  m_Panels.push_back(std::move(cameraPreviewPanel));

  m_PanelConnections.reserve(m_Panels.size());
  for (auto& panel : m_Panels) {
    m_PanelConnections.push_back(m_EventRouter.Subscribe(
        *panel, [this](std::unique_ptr<Engine::Events::Event> event) -> void { ProcessPanelEvent(std::move(event)); }));
  }
  BroadcastSceneChanged();
  AssetRegistryChangedEvent assets(GetAssetManager()->GetKnownModels(), GetAssetManager()->GetKnownTextures(),
                                   GetAssetManager()->GetKnownMaterials());
  BroadcastEvent(assets);
}

void Editor::OnSceneReplaced(const std::shared_ptr<Engine::Scene>& scene) {
  DestroyCameraPreview();
  if (m_CameraPreviewPanel != nullptr) {
    m_CameraPreviewPanel->Close();
  }
  m_ActiveScene = scene;
  m_SelectedEntity = Engine::InvalidEntityHandle;
  BroadcastSceneChanged();
}

void Editor::OnUpdate(float deltaTime) {
  if (m_RenderOutputExportService != nullptr) {
    m_RenderOutputExportService->Update();
  }
  if (!m_ActiveScene) {
    return;
  }
  for (const auto& result : m_ActiveScene->sync()) {
    if (!result.Outcome) {
      ENGINE_LOG_WARN("Scene", "Staged editor command rejected | sequence: {} | reason: {}", result.Sequence,
                      result.Outcome.error().Message);
      continue;
    }
    if (result.EntityCreated) {
      m_SelectedEntity = result.Entity;
      EcsEntitySelectedEvent selected(result.Entity);
      BroadcastEvent(selected);
    }
    BroadcastSceneChanged();
  }
  if (m_SelectedEntity != Engine::InvalidEntityHandle && !m_ActiveScene->World().IsAlive(m_SelectedEntity)) {
    m_SelectedEntity = Engine::InvalidEntityHandle;
    EntityDeselectedEvent deselected;
    BroadcastEvent(deselected);
  }
  UpdateEditorCamera(deltaTime);
}

void Editor::UpdateEditorCamera(float deltaTime) {
  if (!m_ViewportHovered) {
    return;
  }
  Engine::Ecs::LocalTransform updated = m_EditorCameraTransform;
  bool changed = false;
  if (m_MouseDeltaX != 0.0 || m_MouseDeltaY != 0.0) {
    std::array<float, 3> pitchYawRoll = QuaternionToPitchYawRoll(updated.Rotation);
    pitchYawRoll[0] = std::clamp(pitchYawRoll[0] + (static_cast<float>(m_MouseDeltaY) * 0.0025F), -1.55F, 1.55F);
    pitchYawRoll[1] += static_cast<float>(m_MouseDeltaX) * 0.0025F;
    const DirectX::XMVECTOR newRotation =
        DirectX::XMQuaternionRotationRollPitchYaw(pitchYawRoll[0], pitchYawRoll[1], pitchYawRoll[2]);
    DirectX::XMFLOAT4 rotation{};
    DirectX::XMStoreFloat4(&rotation, newRotation);
    updated.Rotation = {rotation.x, rotation.y, rotation.z, rotation.w};
    changed = true;
    m_MouseDeltaX = 0.0;
    m_MouseDeltaY = 0.0;
  }
  using Engine::Events::KeyCode;
  const auto isDown = [this](KeyCode key) -> bool { return m_CameraKeysDown[static_cast<size_t>(key)]; };
  DirectX::XMVECTOR movement = DirectX::XMVectorZero();
  const DirectX::XMVECTOR rotation = DirectX::XMQuaternionNormalize(
      DirectX::XMVectorSet(updated.Rotation[0], updated.Rotation[1], updated.Rotation[2], updated.Rotation[3]));
  const DirectX::XMVECTOR forward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0.0F, 0.0F, 1.0F, 0.0F), rotation);
  const DirectX::XMVECTOR right = DirectX::XMVector3Rotate(DirectX::XMVectorSet(1.0F, 0.0F, 0.0F, 0.0F), rotation);
  const DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F);
  if (isDown(KeyCode::W)) {
    movement = DirectX::XMVectorAdd(movement, forward);
  }
  if (isDown(KeyCode::S)) {
    movement = DirectX::XMVectorSubtract(movement, forward);
  }
  if (isDown(KeyCode::D)) {
    movement = DirectX::XMVectorAdd(movement, right);
  }
  if (isDown(KeyCode::A)) {
    movement = DirectX::XMVectorSubtract(movement, right);
  }
  if (isDown(KeyCode::E)) {
    movement = DirectX::XMVectorAdd(movement, up);
  }
  if (isDown(KeyCode::Q)) {
    movement = DirectX::XMVectorSubtract(movement, up);
  }
  if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(movement)) < 1.0e-6F || deltaTime <= 0.0F) {
    if (changed) {
      m_EditorCameraTransform = updated;
    }
    return;
  }

  const float speed = isDown(KeyCode::LeftShift) ? 15.0F : 5.0F;
  movement = DirectX::XMVectorScale(DirectX::XMVector3Normalize(movement), speed * deltaTime);
  const DirectX::XMVECTOR position = DirectX::XMVectorAdd(
      DirectX::XMVectorSet(updated.Position[0], updated.Position[1], updated.Position[2], 0.0F), movement);
  DirectX::XMFLOAT3 movedPosition{};
  DirectX::XMStoreFloat3(&movedPosition, position);
  updated.Position = {movedPosition.x, movedPosition.y, movedPosition.z};
  m_EditorCameraTransform = updated;
}

void Editor::OnSetupViews() {
  if (!m_ActiveScene || !m_EditorViewport) {
    return;
  }
  Engine::ViewportBinding binding;
  binding.Camera.Position = m_EditorCameraTransform.Position;
  binding.Camera.Rotation = m_EditorCameraTransform.Rotation;
  binding.Camera.FieldOfViewDegrees = m_EditorCameraSettings.FieldOfViewDegrees;
  binding.Camera.NearPlane = m_EditorCameraSettings.NearPlane;
  binding.Camera.FarPlane = m_EditorCameraSettings.FarPlane;
  binding.TargetViewport = m_EditorViewport.get();
  binding.TargetScene = m_ActiveScene.get();
  binding.DebugSettings.ShowLightGizmos = m_EditorPreferences.ShowLightGizmos;
  binding.DebugSettings.ShowCameraFrustums = m_EditorPreferences.ShowCameraFrustums;
  binding.DebugSettings.ShowWireframePlane = m_EditorPreferences.ShowWireframePlane;
  RegisterView(binding);

  UpdateCameraPreview();
  if (m_CameraPreviewViewport != nullptr && m_CameraPreviewEntity != Engine::InvalidEntityHandle) {
    Engine::ViewportBinding previewBinding;
    previewBinding.Camera = m_CameraPreviewCamera;
    previewBinding.RenderMode = Engine::ViewportBinding::Mode::Preview;
    previewBinding.TargetViewport = m_CameraPreviewViewport.get();
    previewBinding.TargetScene = m_ActiveScene.get();
    if (const auto* postProcessing =
            m_ActiveScene->World().Get<Engine::Ecs::CameraPostProcessing>(m_CameraPreviewEntity);
        postProcessing != nullptr) {
      previewBinding.PostProcessing = *postProcessing;
    }
    if (const auto* camera = m_ActiveScene->World().Get<Engine::Ecs::Camera>(m_CameraPreviewEntity);
        camera != nullptr) {
      previewBinding.HasAmbientLightOverride = camera->AmbientLightOverride.has_value();
      if (camera->AmbientLightOverride) {
        previewBinding.AmbientLightOverride = *camera->AmbientLightOverride;
      }
    }
    RegisterView(previewBinding);
  }
}

void Editor::OnRender() {}

void Editor::OnEvent(Engine::Events::Event& event) {
  using namespace Engine::Events;
  EventDispatcher dispatcher(event);
  dispatcher.Dispatch<KeyPressedEvent>([this](const auto& pressed) -> bool {
    const size_t key = static_cast<size_t>(pressed.Key);
    if (m_ViewportHovered && key < m_CameraKeysDown.size()) {
      m_CameraKeysDown[key] = true;
    }
    return false;
  });
  dispatcher.Dispatch<KeyReleasedEvent>([this](const auto& released) -> bool {
    const size_t key = static_cast<size_t>(released.Key);
    if (key < m_CameraKeysDown.size()) {
      m_CameraKeysDown[key] = false;
    }
    return false;
  });
  dispatcher.Dispatch<MouseButtonPressedEvent>([this](const auto& pressed) -> bool {
    m_CameraLookActive = m_ViewportHovered && pressed.Button == MouseButton::Right;
    return false;
  });
  dispatcher.Dispatch<MouseButtonReleasedEvent>([this](const auto& released) -> bool {
    if (released.Button == MouseButton::Right) {
      m_CameraLookActive = false;
    }
    return false;
  });
  dispatcher.Dispatch<MouseMovedEvent>([this](const auto& moved) -> bool {
    if (!m_CameraLookActive || !m_ActiveScene) {
      return false;
    }
    m_MouseDeltaX += moved.DeltaX;
    m_MouseDeltaY += moved.DeltaY;
    return false;
  });
  m_EventRouter.DispatchInput(event);
}

void Editor::OnUIRender() {
  if (!m_UiIntegration.IsInitialized()) {
    return;
  }
  const ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
  ImGui::DockSpaceOverViewport(dockspaceId, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
  BuildDefaultDockLayoutIfNeeded(dockspaceId);
  for (auto& panel : m_Panels) {
    panel->Draw();
  }
  if (m_ActiveScene) {
    for (const auto& result : m_ActiveScene->sync()) {
      if (!result.Outcome) {
        ENGINE_LOG_WARN("Scene", "Staged editor command rejected | sequence: {} | reason: {}", result.Sequence,
                        result.Outcome.error().Message);
        continue;
      }
      if (result.EntityCreated) {
        m_SelectedEntity = result.Entity;
        EcsEntitySelectedEvent selected(result.Entity);
        BroadcastEvent(selected);
      }
      BroadcastSceneChanged();
    }
  }
  m_EditorConsole.UpdateAndRender(&m_EngineConsoleOpen);
}

void Editor::OnBeginUIFrame() { m_UiIntegration.BeginFrame(); }
void Editor::OnEndUIFrame() { m_UiIntegration.EndFrame(); }

void Editor::OnShutdownUI() {
  if (m_RenderOutputExportService != nullptr) {
    m_RenderOutputExportService->Shutdown();
    m_RenderOutputExportService.reset();
  }
  DestroyCameraPreview();
  if (m_CameraPreviewPanel != nullptr) {
    m_CameraPreviewPanel->Close();
    m_CameraPreviewPanel = nullptr;
  }
  m_EditorConsole.DetachFromLogger();
  m_UiIntegration.Shutdown();
}

void Editor::OnResize(uint32_t width, uint32_t height) { CreateViewport(width, height); }

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void Editor::ProcessPanelEvent(std::unique_ptr<Engine::Events::Event> event) {
  if (!event) {
    return;
  }
  Engine::Events::EventDispatcher dispatcher(*event);
  dispatcher.Dispatch<SceneAmbientLightChangedEvent>([this](const auto& changed) -> bool {
    if (m_ActiveScene != nullptr) {
      const Engine::Status status = m_ActiveScene->SetAmbientLight(changed.Settings);
      if (!status) {
        ENGINE_LOG_ERROR("Render", "Scene ambient light update was rejected | reason: {}", status.error().Message);
      }
    }
    return true;
  });
  dispatcher.Dispatch<SceneFileActionRequestedEvent>([this](const auto& request) -> bool {
    if (GetAssetManager() == nullptr) {
      ENGINE_LOG_ERROR("Asset", "Cannot process the file action because the asset manager is unavailable");
      return true;
    }
    if (request.GetAction() == SceneFileActionRequestedEvent::Action::NewScene) {
      std::string error;
      std::vector<std::string> warnings;
      if (!LoadSceneDocument(Engine::SceneDocument{}, error, warnings)) {
        ENGINE_LOG_ERROR("Scene", "Could not create a new scene | reason: {}", error);
      } else {
        m_ActiveSceneDocument.Reset();
      }
      return true;
    }
    if (request.GetAction() == SceneFileActionRequestedEvent::Action::OpenProject) {
      const auto selected = EditorPlatform::ChooseProjectFolder(GetAssetManager()->GetProjectRoot());
      std::vector<std::string> warnings;
      std::string error;
      const EditorWorkflow::ProjectOpenResult result =
          EditorWorkflow::OpenProject(*GetAssetManager(), selected, warnings, error);
      if (result == EditorWorkflow::ProjectOpenResult::Failed) {
        ENGINE_LOG_ERROR("Asset", "Project selection failed | reason: {}", error);
      } else if (result == EditorWorkflow::ProjectOpenResult::Opened) {
        EditorPlatform::RememberProjectRoot(GetAssetManager()->GetProjectRoot());
        for (const std::string& warning : warnings) {
          ENGINE_LOG_WARN("Asset", "Project asset warning | {}", warning);
        }
        AssetRegistryChangedEvent changed(GetAssetManager()->GetKnownModels(), GetAssetManager()->GetKnownTextures(),
                                          GetAssetManager()->GetKnownMaterials());
        BroadcastEvent(changed);
      }
      return true;
    }
    if (request.GetAction() == SceneFileActionRequestedEvent::Action::OpenScene) {
      const auto selected =
          EditorPlatform::ChooseSceneFile(false, m_ActiveSceneDocument.GetPath(), GetAssetManager()->GetProjectRoot());
      std::string error;
      std::vector<std::string> warnings;
      const auto result = m_ActiveSceneDocument.Open(
          selected,
          [this](const std::filesystem::path& path, std::string& loadError,
                 std::vector<std::string>& loadWarnings) -> bool {
            const Engine::SceneDocumentResult loaded = GetAssetManager()->LoadScene(path);
            if (!loaded) {
              loadError = loaded.error;
              loadWarnings.clear();
              return false;
            }
            std::vector<std::string> parseWarnings = loaded.warnings;
            if (!LoadSceneDocument(loaded.document, loadError, loadWarnings)) {
              return false;
            }
            loadWarnings.insert(loadWarnings.end(), parseWarnings.begin(), parseWarnings.end());
            return true;
          },
          error, warnings);
      if (result == EditorSceneDocument::OpenResult::Failed) {
        ENGINE_LOG_ERROR("Scene", "Scene selection failed | path: {} | reason: {}", selected->generic_string(), error);
      } else if (result == EditorSceneDocument::OpenResult::Opened) {
        for (const std::string& warning : warnings) {
          ENGINE_LOG_WARN("Scene", "Scene loaded with warning | {}", warning);
        }
      }
      return true;
    }
    if (request.GetAction() == SceneFileActionRequestedEvent::Action::SaveScene ||
        request.GetAction() == SceneFileActionRequestedEvent::Action::SaveSceneAs) {
      if (m_ActiveScene == nullptr) {
        ENGINE_LOG_ERROR("Scene", "Cannot save a scene because no scene is active");
        return true;
      }
      const bool saveAs = request.GetAction() == SceneFileActionRequestedEvent::Action::SaveSceneAs ||
                          m_ActiveSceneDocument.GetPath().empty();
      const std::filesystem::path destination =
          saveAs ? EditorPlatform::ChooseSceneFile(true, m_ActiveSceneDocument.GetPath(),
                                                   GetAssetManager()->GetProjectRoot())
                       .value_or(std::filesystem::path{})
                 : m_ActiveSceneDocument.GetPath();
      if (destination.empty()) {
        return true;
      }
      const Engine::SceneDocument snapshot = CreateSceneDocument(*m_ActiveScene);
      const Engine::Status saveStatus = m_ActiveSceneDocument.Save(*GetAssetManager(), snapshot, destination);
      if (!saveStatus) {
        ENGINE_LOG_ERROR("Scene", "Scene save failed | path: {} | reason: {}", destination.generic_string(),
                         saveStatus.error().Message);
      } else {
        ENGINE_LOG_INFO("Scene", "Scene saved | path: {}", destination.generic_string());
      }
      return true;
    }
    if (request.GetAction() == SceneFileActionRequestedEvent::Action::ResetSandbox) {
      const std::filesystem::path projectRoot = GetAssetManager()->GetProjectRoot();
      const std::filesystem::path templatePath = projectRoot / "Sandbox.template.scene.json";
      std::string error;
      std::vector<std::string> warnings;
      const auto result = m_ActiveSceneDocument.Open(
          templatePath,
          [this](const std::filesystem::path& path, std::string& loadError,
                 std::vector<std::string>& loadWarnings) -> bool {
            const Engine::SceneDocumentResult loaded = GetAssetManager()->LoadScene(path);
            if (!loaded) {
              loadError = loaded.error;
              loadWarnings.clear();
              return false;
            }
            std::vector<std::string> parseWarnings = loaded.warnings;
            if (!LoadSceneDocument(loaded.document, loadError, loadWarnings)) {
              return false;
            }
            loadWarnings.insert(loadWarnings.end(), parseWarnings.begin(), parseWarnings.end());
            return true;
          },
          error, warnings);
      if (result == EditorSceneDocument::OpenResult::Failed) {
        ENGINE_LOG_ERROR("Scene", "Sandbox reset failed | path: {} | reason: {}", templatePath.generic_string(), error);
      } else if (result == EditorSceneDocument::OpenResult::Opened) {
        m_ActiveSceneDocument.CommitPath(projectRoot / "Sandbox.scene.json");
        for (const std::string& warning : warnings) {
          ENGINE_LOG_WARN("Scene", "Sandbox loaded with warning | {}", warning);
        }
      }
      return true;
    }
    return false;
  });
  if (!m_ActiveScene) {
    return;
  }
  dispatcher.Dispatch<ViewportResizeRequestedEvent>([this](const auto& request) -> bool {
    const uint32_t width = request.GetWidth();
    const uint32_t height = request.GetHeight();
    if (width > 0 && height > 0 &&
        (m_EditorViewportTarget == nullptr || m_EditorViewportTarget->GetWidth() != width ||
         m_EditorViewportTarget->GetHeight() != height)) {
      CreateViewport(width, height);
    }
    return true;
  });
  dispatcher.Dispatch<ViewportHoveredEvent>([this](const auto& hover) -> bool {
    m_ViewportHovered = hover.IsHovered();
    if (!m_ViewportHovered) {
      m_CameraLookActive = false;
      m_CameraKeysDown.fill(false);
      m_MouseDeltaX = 0.0;
      m_MouseDeltaY = 0.0;
    }
    return true;
  });
  dispatcher.Dispatch<EcsEntitySelectedEvent>([this, &event](const auto& selected) -> bool {
    m_SelectedEntity = selected.GetEntity();
    BroadcastEvent(*event);
    return true;
  });
  dispatcher.Dispatch<EntityDeselectedEvent>([this, &event](const auto&) -> bool {
    m_SelectedEntity = Engine::InvalidEntityHandle;
    BroadcastEvent(*event);
    return true;
  });
  dispatcher.Dispatch<CameraPreviewOpenRequestedEvent>([this, &event](const auto&) -> bool {
    BroadcastEvent(*event);
    return true;
  });
  dispatcher.Dispatch<AssetImportRequestedEvent>([this](const auto& request) -> bool {
    if (GetAssetManager() == nullptr) {
      return true;
    }
    if (request.GetAssetType() == AssetImportRequestedEvent::AssetType::Material) {
      auto created = GetAssetManager()->CreateMaterial("Material");
      if (!created) {
        AssetWorkflowFeedbackEvent feedback(false, created.error().Message);
        BroadcastEvent(feedback);
        return true;
      }
      AssetRegistryChangedEvent changed(GetAssetManager()->GetKnownModels(), GetAssetManager()->GetKnownTextures(),
                                        GetAssetManager()->GetKnownMaterials());
      BroadcastEvent(changed);
      if (request.GetTargetEntity() != Engine::InvalidEntityHandle && m_ActiveScene &&
          m_ActiveScene->World().IsAlive(request.GetTargetEntity())) {
        std::string error;
        const auto& guid = created.value();
        if (!GetAssetManager()->LoadMaterial(guid, error)) {
          AssetWorkflowFeedbackEvent feedback(false, error);
          BroadcastEvent(feedback);
          return true;
        }
        auto* current = m_ActiveScene->World().Get<Engine::Ecs::MeshRenderer>(request.GetTargetEntity());
        const auto material = GetAssetManager()->GetMaterial(guid);
        if (current == nullptr || !material) {
          AssetWorkflowFeedbackEvent feedback(false, "Selected entity has no assignable Mesh Renderer");
          BroadcastEvent(feedback);
          return true;
        }
        Engine::Ecs::MeshRenderer updated = *current;
        AssignMaterialAsset(updated, guid, *material);
        const Engine::Status status = m_ActiveScene->SetInPlace(request.GetTargetEntity(), std::move(updated));
        if (!status) {
          AssetWorkflowFeedbackEvent feedback(false, status.error().Message);
          BroadcastEvent(feedback);
          return true;
        }
        BroadcastSceneChanged();
      }
      AssetWorkflowFeedbackEvent feedback(true, "Material asset created");
      BroadcastEvent(feedback);
      return true;
    }
    const auto path = request.GetAssetType() == AssetImportRequestedEvent::AssetType::Texture
                          ? EditorPlatform::ChooseTextureFile(GetAssetManager()->GetProjectRoot())
                          : EditorPlatform::ChooseModelFile(GetAssetManager()->GetProjectRoot());
    if (!path) {
      return true;
    }
    auto imported = request.GetAssetType() == AssetImportRequestedEvent::AssetType::Texture
                        ? GetAssetManager()->ImportTexture(*path)
                        : GetAssetManager()->ImportModel(*path);
    if (!imported) {
      ENGINE_LOG_ERROR("Asset", "Asset import failed | path: {} | reason: {}", path->generic_string(),
                       imported.error().Message);
      return true;
    }
    std::vector<std::string> warnings;
    std::string error;
    if (!GetAssetManager()->RefreshRegistry(warnings, error)) {
      ENGINE_LOG_ERROR("Asset", "Asset registry refresh failed after import | reason: {}", error);
      return true;
    }
    AssetRegistryChangedEvent changed(GetAssetManager()->GetKnownModels(), GetAssetManager()->GetKnownTextures(),
                                      GetAssetManager()->GetKnownMaterials());
    BroadcastEvent(changed);
    return true;
  });
  dispatcher.Dispatch<AssetRenameRequestedEvent>([this](const auto& request) -> bool {
    if (GetAssetManager() == nullptr) {
      const std::string error = "asset manager is unavailable; open a project before renaming assets";
      ENGINE_LOG_ERROR("Asset", "Asset rename failed | reason: {}", error);
      AssetWorkflowFeedbackEvent feedback(false, error);
      BroadcastEvent(feedback);
      return true;
    }
    std::string error;
    if (!GetAssetManager()->RenameAsset(request.GetGuid(), request.GetNewName(), error)) {
      ENGINE_LOG_ERROR("Asset", "Asset rename failed | guid: {} | reason: {}", request.GetGuid(), error);
      AssetWorkflowFeedbackEvent feedback(false, error);
      BroadcastEvent(feedback);
      return true;
    }
    AssetRegistryChangedEvent changed(GetAssetManager()->GetKnownModels(), GetAssetManager()->GetKnownTextures(),
                                      GetAssetManager()->GetKnownMaterials());
    BroadcastEvent(changed);
    AssetWorkflowFeedbackEvent feedback(true, "Asset renamed; scene references remain valid");
    BroadcastEvent(feedback);
    return true;
  });
  dispatcher.Dispatch<MaterialAssetSelectionRequestedEvent>([this](const auto& request) -> bool {
    const auto fail = [this](std::string message) -> bool {
      AssetWorkflowFeedbackEvent feedback(false, std::move(message));
      BroadcastEvent(feedback);
      return true;
    };
    if (GetAssetManager() == nullptr || !m_ActiveScene || !m_ActiveScene->World().IsAlive(request.GetEntity())) {
      return fail("Material assignment requires an open project and a live mesh entity");
    }
    auto* current = m_ActiveScene->World().Get<Engine::Ecs::MeshRenderer>(request.GetEntity());
    if (current == nullptr) {
      return fail("The selected entity does not have a Mesh Renderer component");
    }
    std::string error;
    if (!GetAssetManager()->LoadMaterial(request.GetGuid(), error)) {
      return fail(error);
    }
    const auto material = GetAssetManager()->GetMaterial(request.GetGuid());
    if (!material) {
      return fail("Material asset could not be read after loading");
    }
    if (GetRenderSystem() != nullptr) {
      for (const auto& [textureGuid, srgb, normalMap, preserveAlphaCoverage, alphaCutoff] :
           {std::tuple{material->baseColorTextureGuid, true, false, material->alphaMode == "mask",
                       material->alphaCutoff},
            std::tuple{material->normalTextureGuid, false, true, false, 0.5F}}) {
        if (!textureGuid) {
          continue;
        }
        if (!GetRenderSystem()->PrepareAssetTexture(*textureGuid, srgb, error, normalMap, preserveAlphaCoverage,
                                                    alphaCutoff)) {
          return fail(error);
        }
      }
    }
    Engine::Ecs::MeshRenderer updated = *current;
    AssignMaterialAsset(updated, request.GetGuid(), *material);
    const Engine::Status status = m_ActiveScene->SetInPlace(request.GetEntity(), std::move(updated));
    if (!status) {
      return fail(status.error().Message);
    }
    BroadcastSceneChanged();
    AssetWorkflowFeedbackEvent feedback(true, "Material assigned to Mesh Renderer");
    BroadcastEvent(feedback);
    return true;
  });
  dispatcher.Dispatch<MaterialAssetEditRequestedEvent>([this](const auto& request) -> bool {
    if (GetAssetManager() == nullptr || !m_ActiveScene) {
      AssetWorkflowFeedbackEvent feedback(false, "Material edits require an open project");
      BroadcastEvent(feedback);
      return true;
    }
    std::string error;
    if (GetRenderSystem() != nullptr) {
      for (const auto& [textureGuid, srgb, normalMap, preserveAlphaCoverage, alphaCutoff] :
           {std::tuple{request.GetMaterial().baseColorTextureGuid, true, false,
                       request.GetMaterial().alphaMode == "mask", request.GetMaterial().alphaCutoff},
            std::tuple{request.GetMaterial().normalTextureGuid, false, true, false, 0.5F}}) {
        if (!textureGuid) {
          continue;
        }
        if (!GetRenderSystem()->PrepareAssetTexture(*textureGuid, srgb, error, normalMap, preserveAlphaCoverage,
                                                    alphaCutoff)) {
          AssetWorkflowFeedbackEvent feedback(false, error);
          BroadcastEvent(feedback);
          return true;
        }
      }
    }
    if (!GetAssetManager()->SaveMaterial(request.GetGuid(), request.GetMaterial(), error)) {
      AssetWorkflowFeedbackEvent feedback(false, error);
      BroadcastEvent(feedback);
      return true;
    }
    std::vector<std::pair<Engine::EntityHandle, Engine::Ecs::MeshRenderer>> updates;
    m_ActiveScene->World().ForEachChunkReadOnly<Engine::Ecs::MeshRenderer>(
        [&updates, &request](std::span<const Engine::EntityHandle> entities,
                             std::span<const Engine::Ecs::MeshRenderer> meshes) -> void {
          for (size_t index = 0; index < entities.size(); ++index) {
            if (meshes[index].MaterialGuid == request.GetGuid()) {
              auto updated = meshes[index];
              if (ApplyMaterialAssetEdit(updated, request.GetGuid(), request.GetMaterial())) {
                updates.emplace_back(entities[index], std::move(updated));
              }
            }
          }
        });
    for (auto& [entity, mesh] : updates) {
      const Engine::Status status = m_ActiveScene->SetInPlace(entity, std::move(mesh));
      if (!status) {
        AssetWorkflowFeedbackEvent feedback(false, status.error().Message);
        BroadcastEvent(feedback);
        return true;
      }
    }
    BroadcastSceneChanged();
    AssetWorkflowFeedbackEvent feedback(true, "Shared material updated");
    BroadcastEvent(feedback);
    return true;
  });
  dispatcher.Dispatch<ModelReassignmentRequestedEvent>([this](const auto& request) -> bool {
    std::string error;
    const auto fail = [this, &error](std::string message) -> bool {
      error = std::move(message);
      ENGINE_LOG_ERROR("Asset", "Model assignment failed | reason: {}", error);
      AssetWorkflowFeedbackEvent feedback(false, error);
      BroadcastEvent(feedback);
      return true;
    };
    if (GetAssetManager() == nullptr) {
      return fail("asset manager is unavailable; open a project before assigning models");
    }
    if (!m_ActiveScene || m_SelectedEntity == Engine::InvalidEntityHandle ||
        !m_ActiveScene->World().IsAlive(m_SelectedEntity)) {
      return fail("Select a live mesh entity before assigning a model");
    }
    auto* current = m_ActiveScene->World().Get<Engine::Ecs::MeshRenderer>(m_SelectedEntity);
    if (current == nullptr) {
      return fail("The selected entity does not have a Mesh Renderer component");
    }
    Engine::Ecs::MeshRenderer updated = *current;
    const auto models = GetAssetManager()->GetKnownModels();
    if (!AssignDroppedModel(updated, request.GetGuid(), models, error)) {
      return fail(error);
    }
    if (!GetAssetManager()->LoadModel(request.GetGuid(), error)) {
      return fail(error);
    }
    if (GetRenderSystem() != nullptr) {
      const auto model = GetAssetManager()->GetModel(request.GetGuid());
      const auto generation = GetAssetManager()->GetGeneration(request.GetGuid());
      if (model == nullptr || !generation ||
          !GetRenderSystem()->PrepareModelResources(request.GetGuid(), *generation, *model, {}, error)) {
        return fail(error.empty() ? "Model resource preparation failed" : error);
      }
    }
    const Engine::Status status = m_ActiveScene->SetInPlace(m_SelectedEntity, std::move(updated));
    if (!status) {
      return fail(status.error().Message);
    }
    BroadcastSceneChanged();
    AssetWorkflowFeedbackEvent feedback(true, "Model assigned to selected Mesh Renderer");
    BroadcastEvent(feedback);
    return true;
  });
  dispatcher.Dispatch<EcsEntityCreateRequestedEvent>([this](const auto& request) -> bool {
    (void)m_ActiveScene->QueueCreate(request.GetName(), request.GetParent(), request.GetPreset());
    return true;
  });
  dispatcher.Dispatch<EcsEntityDeleteRequestedEvent>([this](const auto& request) -> bool {
    (void)m_ActiveScene->QueueDestroy(request.GetEntity());
    return true;
  });
  dispatcher.Dispatch<EcsEntityReparentRequestedEvent>([this](const auto& request) -> bool {
    (void)m_ActiveScene->QueueReparent(request.GetEntity(), request.GetParent());
    return true;
  });
  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  dispatcher.Dispatch<EcsComponentMutationRequestedEvent>([this](auto& request) -> bool {
    const auto entity = request.GetEntity();
    const auto operation = request.GetOperation();
    auto payload = request.TakePayload();
    const auto queueMutation = [&]<typename Component>(Component component) -> void {
      if (operation == EcsComponentMutationRequestedEvent::Operation::Add) {
        (void)m_ActiveScene->QueueAdd(entity, std::move(component));
      } else if (operation == EcsComponentMutationRequestedEvent::Operation::Set) {
        const Engine::Status status = m_ActiveScene->SetInPlace(entity, std::move(component));
        if (!status) {
          ENGINE_LOG_WARN("Scene", "Editor component edit rejected | reason: {}", status.error().Message);
          AssetWorkflowFeedbackEvent feedback(false, status.error().Message);
          BroadcastEvent(feedback);
        }
      }
    };
    switch (request.GetComponent()) {
    case EcsComponentMutationRequestedEvent::Component::Name:
      if (operation == EcsComponentMutationRequestedEvent::Operation::Set) {
        if (const auto* value = std::get_if<Engine::Ecs::Name>(&payload)) {
          queueMutation(*value);
        }
      }
      break;
    case EcsComponentMutationRequestedEvent::Component::Transform:
      if (operation == EcsComponentMutationRequestedEvent::Operation::Set) {
        if (const auto* value = std::get_if<Engine::Ecs::LocalTransform>(&payload)) {
          queueMutation(*value);
        }
      }
      break;
    case EcsComponentMutationRequestedEvent::Component::LocalVisibility:
      if (operation == EcsComponentMutationRequestedEvent::Operation::Set) {
        if (const auto* value = std::get_if<Engine::Ecs::LocalVisibility>(&payload)) {
          queueMutation(*value);
        }
      }
      break;
    case EcsComponentMutationRequestedEvent::Component::MeshRenderer:
      if (operation == EcsComponentMutationRequestedEvent::Operation::Remove) {
        (void)m_ActiveScene->QueueRemove<Engine::Ecs::MeshRenderer>(entity);
      } else if (const auto* value = std::get_if<Engine::Ecs::MeshRenderer>(&payload)) {
        const auto* previous = m_ActiveScene->World().Get<Engine::Ecs::MeshRenderer>(entity);
        if (!value->ModelGuid.empty() && (previous == nullptr || previous->ModelGuid != value->ModelGuid)) {
          std::string error;
          if (GetAssetManager() == nullptr || !GetAssetManager()->LoadModel(value->ModelGuid, error)) {
            if (error.empty()) {
              error = "model asset manager is unavailable";
            }
          } else if (GetRenderSystem() != nullptr) {
            const auto loadedModel = GetAssetManager()->GetModel(value->ModelGuid);
            const auto loadedGeneration = GetAssetManager()->GetGeneration(value->ModelGuid);
            if (loadedModel == nullptr || !loadedGeneration ||
                !GetRenderSystem()->PrepareModelResources(value->ModelGuid, *loadedGeneration, *loadedModel, {},
                                                          error)) {
              if (error.empty()) {
                error = "model resource preparation failed";
              }
            }
          }
          if (!error.empty()) {
            ENGINE_LOG_ERROR("Asset", "Mesh model assignment rejected | guid: {} | reason: {}", value->ModelGuid,
                             error);
            AssetWorkflowFeedbackEvent feedback(false, error);
            BroadcastEvent(feedback);
            return true;
          }
        }
        const auto prepareMaterial = [this](const Engine::SceneMaterialDocument& material) -> bool {
          if (GetRenderSystem() == nullptr) {
            return true;
          }
          const auto prepare = [this](const std::optional<std::string>& guid, bool srgb, bool normalMap,
                                      bool preserveAlphaCoverage, float alphaCutoff,
                                      std::string_view semantic) -> bool {
            if (!guid) {
              return true;
            }
            std::string error;
            if (!GetRenderSystem()->PrepareAssetTexture(*guid, srgb, error, normalMap, preserveAlphaCoverage,
                                                        alphaCutoff)) {
              ENGINE_LOG_ERROR("Asset", "Material texture assignment rejected | slot: {} | guid: {} | reason: {}",
                               semantic, *guid, error);
              AssetWorkflowFeedbackEvent feedback(false, error);
              BroadcastEvent(feedback);
              return false;
            }
            return true;
          };
          return prepare(material.baseColorTextureGuid, true, false, material.alphaMode == "mask", material.alphaCutoff,
                         "base-color") &&
                 prepare(material.normalTextureGuid, false, true, false, 0.5F, "normal");
        };
        bool texturesReady = !value->Material || prepareMaterial(*value->Material);
        for (const auto& material : value->MaterialOverrides) {
          if (material) {
            texturesReady = prepareMaterial(*material) && texturesReady;
          }
        }
        if (!texturesReady) {
          return true;
        }
        queueMutation(*value);
      } else if (operation == EcsComponentMutationRequestedEvent::Operation::Add) {
        queueMutation(Engine::Ecs::MeshRenderer{});
      }
      break;
    case EcsComponentMutationRequestedEvent::Component::SpotLight:
      if (operation == EcsComponentMutationRequestedEvent::Operation::Remove) {
        (void)m_ActiveScene->QueueRemove<Engine::Ecs::SpotLight>(entity);
      } else if (const auto* value = std::get_if<Engine::Ecs::SpotLight>(&payload)) {
        queueMutation(*value);
      } else if (operation == EcsComponentMutationRequestedEvent::Operation::Add) {
        queueMutation(Engine::Ecs::SpotLight{});
      }
      break;
    case EcsComponentMutationRequestedEvent::Component::Camera:
      if (operation == EcsComponentMutationRequestedEvent::Operation::Remove) {
        (void)m_ActiveScene->QueueRemove<Engine::Ecs::Camera>(entity);
      } else if (const auto* value = std::get_if<Engine::Ecs::Camera>(&payload)) {
        queueMutation(*value);
      } else if (operation == EcsComponentMutationRequestedEvent::Operation::Add) {
        queueMutation(Engine::Ecs::Camera{});
      }
      break;
    case EcsComponentMutationRequestedEvent::Component::CameraPostProcessing:
      if (operation == EcsComponentMutationRequestedEvent::Operation::Remove) {
        (void)m_ActiveScene->QueueRemove<Engine::Ecs::CameraPostProcessing>(entity);
      } else if (const auto* value = std::get_if<Engine::Ecs::CameraPostProcessing>(&payload)) {
        queueMutation(*value);
      } else if (operation == EcsComponentMutationRequestedEvent::Operation::Add) {
        queueMutation(Engine::Ecs::CameraPostProcessing{});
      }
      break;
    }
    return true;
  });
}

void Editor::BroadcastEvent(Engine::Events::Event& event) { m_EventRouter.NotifyPanels(event); }

void Editor::BroadcastSceneChanged() {
  SceneChangedEvent event(m_ActiveScene);
  BroadcastEvent(event);
}

void Editor::CreateViewport(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0 || GetRenderSystem() == nullptr) {
    return;
  }
  DestroyViewport();
  auto target = GetRenderSystem()->CreateViewportTarget(width, height);
  if (!target) {
    ENGINE_LOG_ERROR("Render", "Editor viewport target creation failed | width: {} | height: {}", width, height);
    return;
  }
  m_ViewportSize = {static_cast<float>(width), static_cast<float>(height)};
  m_EditorViewportTarget = std::move(target);
  m_EditorViewport = std::make_shared<Engine::Viewport>(m_EditorViewportTarget);
  AddViewport(m_EditorViewport);
  ViewportChangedEvent changed(GetViewportTextureID(), m_ViewportSize.first, m_ViewportSize.second);
  BroadcastEvent(changed);
}

void Editor::UpdateCameraPreview() {
  if (!m_ActiveScene || m_SelectedEntity == Engine::InvalidEntityHandle) {
    DestroyCameraPreview();
    return;
  }

  const Engine::ArchetypeWorld& world = m_ActiveScene->World();
  const auto* camera = world.Get<Engine::Ecs::Camera>(m_SelectedEntity);
  const auto* transform = world.Get<Engine::Ecs::WorldTransform>(m_SelectedEntity);
  if (camera == nullptr || transform == nullptr || camera->OutputWidth == 0 || camera->OutputHeight == 0) {
    DestroyCameraPreview();
    return;
  }
  const auto cameraView = Engine::Renderer::ResolveSceneCameraView(*camera, *transform);
  if (!cameraView) {
    DestroyCameraPreview();
    return;
  }

  const uint32_t longestDimension = (std::max)(camera->OutputWidth, camera->OutputHeight);
  const uint32_t previewMaxDimension =
      static_cast<uint32_t>(static_cast<float>(longestDimension) * m_EditorPreferences.CameraPreviewQuality);
  const float scale =
      (std::min)(1.0F, static_cast<float>((std::max)(previewMaxDimension, 1U)) / static_cast<float>(longestDimension));
  const uint32_t width = (std::max)(1U, static_cast<uint32_t>(static_cast<float>(camera->OutputWidth) * scale));
  const uint32_t height = (std::max)(1U, static_cast<uint32_t>(static_cast<float>(camera->OutputHeight) * scale));
  if (m_CameraPreviewTarget == nullptr || m_CameraPreviewTarget->GetWidth() != width ||
      m_CameraPreviewTarget->GetHeight() != height) {
    auto target = GetRenderSystem() != nullptr ? GetRenderSystem()->CreateViewportTarget(width, height) : nullptr;
    if (target == nullptr) {
      ENGINE_LOG_ERROR("Render", "Selected camera preview target creation failed | width: {} | height: {}", width,
                       height);
      DestroyCameraPreview();
      return;
    }
    if (m_CameraPreviewViewport != nullptr) {
      RemoveViewport(m_CameraPreviewViewport);
    }
    m_CameraPreviewViewport.reset();
    m_CameraPreviewTarget.reset();
    m_CameraPreviewTarget = std::move(target);
    m_CameraPreviewViewport = std::make_shared<Engine::Viewport>(m_CameraPreviewTarget);
    AddViewport(m_CameraPreviewViewport);
  }

  const bool changed = m_CameraPreviewEntity != m_SelectedEntity || m_CameraPreviewResolution.first != width ||
                       m_CameraPreviewResolution.second != height ||
                       m_LastCameraPreviewDisplayScale != m_EditorPreferences.CameraPreviewScale;
  m_CameraPreviewEntity = m_SelectedEntity;
  m_CameraPreviewCamera = *cameraView;
  m_CameraPreviewResolution = {width, height};
  m_LastCameraPreviewDisplayScale = m_EditorPreferences.CameraPreviewScale;
  if (changed) {
    CameraPreviewChangedEvent previewChanged(
        m_CameraPreviewEntity, m_UiIntegration.GetTextureId(m_CameraPreviewTarget.get()), static_cast<float>(width),
        static_cast<float>(height), m_EditorPreferences.CameraPreviewScale);
    BroadcastEvent(previewChanged);
  }
}

void Editor::DestroyCameraPreview() {
  const bool wasVisible = m_CameraPreviewEntity != Engine::InvalidEntityHandle || m_CameraPreviewTarget != nullptr;
  if (m_CameraPreviewViewport != nullptr) {
    RemoveViewport(m_CameraPreviewViewport);
  }
  m_CameraPreviewViewport.reset();
  m_CameraPreviewTarget.reset();
  m_CameraPreviewEntity = Engine::InvalidEntityHandle;
  m_CameraPreviewResolution = {0, 0};
  if (wasVisible) {
    CameraPreviewChangedEvent previewChanged(Engine::InvalidEntityHandle, nullptr, 0.0F, 0.0F, 0.35F);
    BroadcastEvent(previewChanged);
  }
}

void Editor::DestroyViewport() {
  if (m_EditorViewport) {
    RemoveViewport(m_EditorViewport);
  }
  m_EditorViewport.reset();
  m_EditorViewportTarget.reset();
  m_ViewportSize = {0.0F, 0.0F};
}

auto Editor::GetViewportTextureID() const -> void* {
  return m_UiIntegration.GetTextureId(m_EditorViewportTarget.get());
}

void Editor::BuildDefaultDockLayoutIfNeeded(uint32_t dockspaceId) {
  if (m_DefaultLayoutBuilt) {
    return;
  }
  ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
  if (node != nullptr && (node->Windows.Size > 0 || node->ChildNodes[0] != nullptr || node->ChildNodes[1] != nullptr)) {
    m_DefaultLayoutBuilt = true;
    return;
  }
  BuildDefaultDockLayout(dockspaceId);
  m_DefaultLayoutBuilt = true;
}

void Editor::BuildDefaultDockLayout(uint32_t dockspaceId) {
  ImGui::DockBuilderRemoveNode(dockspaceId);
  ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);
  ImGuiID main = dockspaceId;
  ImGuiID left = 0;
  ImGuiID right = 0;
  ImGuiID bottom = 0;
  ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, 0.20F, &left, &main);
  ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.25F, &right, &main);
  ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.25F, &bottom, &main);
  ImGui::DockBuilderDockWindow("Scene", left);
  ImGui::DockBuilderDockWindow("Hierarchy", left);
  ImGui::DockBuilderDockWindow("Inspector", right);
  ImGui::DockBuilderDockWindow("Stats", bottom);
  ImGui::DockBuilderDockWindow("Assets", bottom);
  ImGui::DockBuilderDockWindow("Console", bottom);
  ImGui::DockBuilderDockWindow("Viewport", main);
  ImGui::DockBuilderFinish(dockspaceId);
}

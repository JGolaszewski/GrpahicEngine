#include "application/include/Editor.h"

#include "events/include/AssetRegistryChangedEvent.h"
#include "events/include/EcsComponentMutationRequestedEvent.h"
#include "events/include/EcsEntityCreateRequestedEvent.h"
#include "events/include/EcsEntityDeleteRequestedEvent.h"
#include "events/include/EcsEntityReparentRequestedEvent.h"
#include "events/include/EcsEntitySelectedEvent.h"
#include "events/include/EditorEvents.h"
#include "panels/include/AssetBrowserPanel.h"
#include "panels/include/CameraPreviewPanel.h"
#include "panels/include/CameraPreviewRequest.h"
#include "panels/include/EngineSettingsPanel.h"
#include "panels/include/HierarchyPanel.h"
#include "panels/include/InspectorPanel.h"
#include "panels/include/MainBar.h"
#include "panels/include/RenderPanel.h"
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
    light.UsePosition = true;
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
      (void)renderSystem->CreatePrimitive(Engine::PrimitiveShape::Cube, std::move(material));
    }
  }

  CreateViewport(1280, 720);
  auto mainBar = std::make_unique<MainBar>();
  MainBar* mainBarPanel = mainBar.get();
  mainBar->SetConsoleOpenHandler([this] -> void { m_EngineConsoleOpen = true; });
  auto engineSettings = std::make_unique<EngineSettingsPanel>(this);
  auto* engineSettingsPanel = engineSettings.get();
  mainBar->SetSettingsOpenHandler([engineSettingsPanel] -> void { engineSettingsPanel->Open(); });
  m_Panels.push_back(std::move(mainBar));
  m_Panels.push_back(std::move(engineSettings));
  m_Panels.push_back(std::make_unique<StatWindow>(this));
  m_Panels.push_back(std::make_unique<ScenePanel>());
  m_Panels.push_back(std::make_unique<HierarchyPanel>());
  m_Panels.push_back(std::make_unique<InspectorPanel>());
  m_Panels.push_back(std::make_unique<AssetBrowserPanel>());
  auto renderPanel = std::make_unique<RenderPanel>(
      [this](const Engine::Renderer::CameraView& camera, uint32_t width,
             uint32_t height) -> Engine::Result<Engine::RenderOutput> {
        if (!m_ActiveScene || GetRenderSystem() == nullptr) {
          return std::unexpected(Engine::MakeError("No active scene or render system is available"));
        }
        return GetRenderSystem()->RenderFinal(*m_ActiveScene, camera, width, height);
      },
      [this](uint32_t width, uint32_t height, std::span<const uint8_t> pixels) -> bool {
        return m_UiIntegration.SetRenderPreview(width, height, pixels);
      },
      [this]() -> void* { return m_UiIntegration.GetRenderPreviewTextureId(); });
  m_RenderPanel = renderPanel.get();
  m_RenderPanel->SetScene(m_ActiveScene);
  mainBarPanel->SetRenderOpenHandler([this] -> void {
    if (m_RenderPanel != nullptr) {
      m_RenderPanel->Open();
    }
  });
  m_Panels.push_back(std::move(renderPanel));

  auto cameraPreviewPanel = std::make_unique<CameraPreviewPanel>(
      [this](Engine::EntityHandle entity) -> Engine::Result<Engine::RenderOutput> {
        if (!m_ActiveScene || GetRenderSystem() == nullptr) {
          return std::unexpected(Engine::MakeError("No active scene or render system is available"));
        }
        const auto request = ResolveCameraPreviewRequest(m_ActiveScene->World(), entity);
        if (!request) {
          return std::unexpected(request.error());
        }
        return GetRenderSystem()->RenderFinal(*m_ActiveScene, request->View, request->Width, request->Height);
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
             std::filesystem::path destination) -> Engine::Result<Engine::RenderOutputExportService::RequestId> {
        Engine::RenderSystem* renderer = GetRenderSystem();
        if (!m_ActiveScene || renderer == nullptr || m_RenderOutputExportService == nullptr) {
          return std::unexpected(Engine::MakeError("No active scene or render system is available"));
        }
        const auto request = ResolveCameraPreviewRequest(m_ActiveScene->World(), entity);
        if (!request) {
          return std::unexpected(request.error());
        }
        if (exr) {
          return m_RenderOutputExportService->StartMultilayerExr(*renderer, *m_ActiveScene, request->View,
                                                                 request->Width, request->Height,
                                                                 std::move(selectedAovs), std::move(destination));
        }
        return m_RenderOutputExportService->StartPng(*renderer, *m_ActiveScene, request->View, request->Width,
                                                     request->Height, std::move(destination));
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
  AssetRegistryChangedEvent assets(GetAssetManager()->GetKnownModels(), GetAssetManager()->GetKnownTextures());
  BroadcastEvent(assets);
}

void Editor::OnSceneReplaced(const std::shared_ptr<Engine::Scene>& scene) {
  DestroyCameraPreview();
  if (m_CameraPreviewPanel != nullptr) {
    m_CameraPreviewPanel->Close();
  }
  m_ActiveScene = scene;
  m_SelectedEntity = Engine::InvalidEntityHandle;
  if (m_RenderPanel != nullptr) {
    m_RenderPanel->SetScene(scene);
  }
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
  binding.DebugSettings.ShowLightGizmos = m_ShowDebugLightGizmos;
  binding.DebugSettings.ShowCameraFrustums = true;
  binding.DebugSettings.ShowWireframePlane = m_ShowDebugWireframePlane;
  RegisterView(binding);

  UpdateCameraPreview();
  if (m_CameraPreviewViewport != nullptr && m_CameraPreviewEntity != Engine::InvalidEntityHandle) {
    Engine::ViewportBinding previewBinding;
    previewBinding.Camera = m_CameraPreviewCamera;
    previewBinding.RenderMode = Engine::ViewportBinding::Mode::Preview;
    previewBinding.TargetViewport = m_CameraPreviewViewport.get();
    previewBinding.TargetScene = m_ActiveScene.get();
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
  if (!event || !m_ActiveScene) {
    return;
  }
  Engine::Events::EventDispatcher dispatcher(*event);
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

  constexpr uint32_t previewMaxDimension = 320;
  const uint32_t longestDimension = (std::max)(camera->OutputWidth, camera->OutputHeight);
  const float scale = (std::min)(1.0F, static_cast<float>(previewMaxDimension) / static_cast<float>(longestDimension));
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
                       m_CameraPreviewResolution.second != height;
  m_CameraPreviewEntity = m_SelectedEntity;
  m_CameraPreviewCamera = *cameraView;
  m_CameraPreviewResolution = {width, height};
  if (changed) {
    CameraPreviewChangedEvent previewChanged(m_CameraPreviewEntity,
                                             m_UiIntegration.GetTextureId(m_CameraPreviewTarget.get()),
                                             static_cast<float>(width), static_cast<float>(height));
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
    CameraPreviewChangedEvent previewChanged(Engine::InvalidEntityHandle, nullptr, 0.0F, 0.0F);
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
  ImGui::DockBuilderDockWindow("Scene Hierarchy", left);
  ImGui::DockBuilderDockWindow("Inspector", right);
  ImGui::DockBuilderDockWindow("Statistics", bottom);
  ImGui::DockBuilderDockWindow("Assets", bottom);
  ImGui::DockBuilderDockWindow("Render", right);
  ImGui::DockBuilderDockWindow("Engine Console", bottom);
  ImGui::DockBuilderDockWindow("Viewport", main);
  ImGui::DockBuilderFinish(dockspaceId);
}

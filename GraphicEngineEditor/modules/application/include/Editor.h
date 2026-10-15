#pragma once

#include "application/include/EditorEventRouter.h"
#include "panels/include/EditorConsole.h"
#include "panels/include/EditorPanel.h"
#include "platform/include/EditorUiIntegration.h"
#include "rendering/include/renderer/CameraView.h"
#include "runtime/include/Application.h"
#include "scene/include/Scene.h"

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace Engine {
class Viewport;
class ViewportTarget;
} // namespace Engine
class RenderPanel;

class Editor final : public Engine::Application {
public:
  Editor() = default;
  ~Editor() override = default;

protected:
  void OnInit() override;
  void OnRender() override;
  void OnUpdate(float deltaTime) override;
  void OnUIRender() override;
  void OnBeginUIFrame() override;
  void OnEndUIFrame() override;
  void OnShutdownUI() override;
  void OnResize(uint32_t width, uint32_t height) override;
  void OnEvent(Engine::Events::Event& event) override;
  void OnSetupViews() override;
  void OnSceneReplaced(const std::shared_ptr<Engine::Scene>& scene) override;

private:
  void ProcessPanelEvent(std::unique_ptr<Engine::Events::Event> event);
  void BroadcastEvent(Engine::Events::Event& event);
  void BroadcastSceneChanged();
  void CreateViewport(uint32_t width, uint32_t height);
  void DestroyViewport();
  void UpdateCameraPreview();
  void DestroyCameraPreview();
  void BuildDefaultDockLayoutIfNeeded(uint32_t dockspaceId);
  static void BuildDefaultDockLayout(uint32_t dockspaceId);
  void UpdateEditorCamera(float deltaTime);
  [[nodiscard]] auto GetViewportTextureID() const -> void*;

  std::shared_ptr<Engine::Scene> m_ActiveScene;
  std::shared_ptr<Engine::Viewport> m_EditorViewport;
  std::shared_ptr<Engine::ViewportTarget> m_EditorViewportTarget;
  std::shared_ptr<Engine::Viewport> m_CameraPreviewViewport;
  std::shared_ptr<Engine::ViewportTarget> m_CameraPreviewTarget;
  std::vector<std::unique_ptr<EditorPanel>> m_Panels;
  RenderPanel* m_RenderPanel = nullptr;
  EditorEventRouter m_EventRouter;
  std::vector<EditorEventRouter::Connection> m_PanelConnections;
  EditorUiIntegration m_UiIntegration;
  EditorConsole m_EditorConsole;
  Engine::EntityHandle m_SelectedEntity = Engine::InvalidEntityHandle;
  Engine::EntityHandle m_CameraPreviewEntity = Engine::InvalidEntityHandle;
  Engine::Renderer::CameraView m_CameraPreviewCamera;
  std::pair<uint32_t, uint32_t> m_CameraPreviewResolution = {0, 0};
  Engine::Ecs::LocalTransform m_EditorCameraTransform{.Position = {0.0F, 0.0F, -5.0F}};
  Engine::Ecs::Camera m_EditorCameraSettings;
  std::pair<float, float> m_ViewportSize = {0.0F, 0.0F};
  bool m_DefaultLayoutBuilt = false;
  bool m_EngineConsoleOpen = true;
  bool m_ShowDebugLightGizmos = true;
  bool m_ShowDebugWireframePlane = true;
  std::array<bool, static_cast<size_t>(Engine::Events::KeyCode::Count)> m_CameraKeysDown{};
  bool m_ViewportHovered = false;
  bool m_CameraLookActive = false;
  double m_MouseDeltaX = 0.0;
  double m_MouseDeltaY = 0.0;
};

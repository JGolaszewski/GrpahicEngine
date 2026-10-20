#pragma once

#include "application/include/EditorEventRouter.h"
#include "application/include/EditorSceneDocument.h"
#include "application/include/SnapshotAnalysisFocusState.h"
#include "panels/include/EditorConsole.h"
#include "panels/include/EditorPanel.h"
#include "panels/include/MainBar.h"
#include "panels/include/SnapshotAnalysisPanel.h"
#include "platform/include/EditorFileDialogs.h"
#include "platform/include/EditorUiIntegration.h"
#include "rendering/include/RenderOutputExportService.h"
#include "rendering/include/renderer/CameraView.h"
#include "runtime/include/Application.h"
#include "scene/include/Scene.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Engine {
class Viewport;
class ViewportTarget;
} // namespace Engine
class CameraPreviewPanel;
class BrdfAnalysisPanel;
class AnalyticalFramePanel;
class MainBar;

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4324) // The editor camera transform requires 16-byte alignment.
#endif
class Editor final : public Engine::Application {
public:
  Editor() = default;
  ~Editor() override;

protected:
  void OnInit() override;
  void OnRender() override;
  void OnUpdate(float deltaTime) override;
  void OnUIRender() override;
  void OnBeginUIFrame() override;
  void OnEndUIFrame() override;
  void OnShutdownUI() override;
  void OnEvent(Engine::Events::Event& event) override;
  void OnSetupViews() override;
  bool ShouldRegisterDefaultView() const override;
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
  void SetSnapshotAnalysisFocused(bool focused);
  [[nodiscard]] auto GetViewportTextureID() const -> void*;

  std::shared_ptr<Engine::Scene> m_ActiveScene;
  EditorSceneDocument m_ActiveSceneDocument;
  std::shared_ptr<Engine::Viewport> m_EditorViewport;
  std::shared_ptr<Engine::ViewportTarget> m_EditorViewportTarget;
  std::shared_ptr<Engine::Viewport> m_CameraPreviewViewport;
  std::shared_ptr<Engine::ViewportTarget> m_CameraPreviewTarget;
  std::vector<std::unique_ptr<EditorPanel>> m_Panels;
  std::unique_ptr<MainBar> m_MainBar;
  std::unique_ptr<SnapshotAnalysisPanel> m_SnapshotAnalysisPanel;
  CameraPreviewPanel* m_CameraPreviewPanel = nullptr;
  BrdfAnalysisPanel* m_BrdfAnalysisPanel = nullptr;
  AnalyticalFramePanel* m_AnalyticalFramePanel = nullptr;
  EditorEventRouter m_EventRouter;
  std::vector<EditorEventRouter::Connection> m_PanelConnections;
  EditorUiIntegration m_UiIntegration;
  EditorConsole m_EditorConsole;
  std::unique_ptr<Engine::RenderOutputExportService> m_RenderOutputExportService;
  SnapshotAnalysisFocusState m_SnapshotAnalysisFocusState;
  std::string m_PreviousFocusedWindow;
  std::string m_WindowToRestoreFocus;
  Engine::EntityHandle m_SelectedEntity = Engine::InvalidEntityHandle;
  Engine::EntityHandle m_CameraPreviewEntity = Engine::InvalidEntityHandle;
  Engine::Renderer::CameraView m_CameraPreviewCamera;
  std::pair<uint32_t, uint32_t> m_CameraPreviewResolution = {0, 0};
  float m_LastCameraPreviewDisplayScale = 0.0F;
  Engine::Ecs::LocalTransform m_EditorCameraTransform{.Position = {0.0F, 0.0F, -5.0F}};
  Engine::Ecs::Camera m_EditorCameraSettings;
  std::pair<float, float> m_ViewportSize = {0.0F, 0.0F};
  bool m_DefaultLayoutBuilt = false;
  bool m_RestoreSnapshotFocus = false;
  bool m_EngineConsoleOpen = true;
  bool m_AnalyticalFrameWorkspaceActive = false;
  EditorPlatform::EditorPreferences m_EditorPreferences;
  std::array<bool, static_cast<size_t>(Engine::Events::KeyCode::Count)> m_CameraKeysDown{};
  bool m_ViewportHovered = false;
  bool m_CameraLookActive = false;
  double m_MouseDeltaX = 0.0;
  double m_MouseDeltaY = 0.0;
};
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

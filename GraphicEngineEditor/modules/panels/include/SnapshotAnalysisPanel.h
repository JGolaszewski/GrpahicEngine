#pragma once

#include "panels/include/EditorPanel.h"
#include "panels/include/SnapshotAnalysisPanelState.h"
#include "rendering/include/RenderSystem.h"
#include "rendering/include/SnapshotFrame.h"
#include "rendering/include/analysis/SnapshotAnalysis.h"
#include "rendering/include/persistence/SnapshotPersistence.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

/// Static, stored-buffer viewer and analysis workspace for immutable captured frames.
class SnapshotAnalysisPanel final : public EditorPanel {
public:
  /// Starts an authored-camera capture with optional frame-local dimensions.
  using BeginCaptureFunction = std::function<Engine::Result<Engine::RenderSystem::SnapshotRenderTicket>(
      Engine::EntityHandle camera, uint32_t widthOverride, uint32_t heightOverride,
      Engine::SnapshotPlaneMask selectedPlanes)>;
  /// Lists valid authored cameras for selection in the focused workspace.
  using ListCamerasFunction = std::function<std::vector<SnapshotAnalysisCameraOption>()>;
  /// Polls a capture without publishing an incomplete frame.
  using PollCaptureFunction =
      std::function<Engine::Result<std::optional<Engine::SnapshotFrame>>(Engine::RenderSystem::SnapshotRenderTicket)>;
  /// Reports capture progress and terminal state.
  using CaptureStatusFunction = std::function<std::optional<Engine::RenderSystem::SnapshotRenderStatus>(
      Engine::RenderSystem::SnapshotRenderTicket)>;
  /// Cancels an outstanding capture ticket.
  using CancelCaptureFunction = std::function<Engine::Status(Engine::RenderSystem::SnapshotRenderTicket)>;
  /// Uploads top-to-bottom RGBA8 display pixels for the active frame tab.
  using UploadImageFunction = std::function<bool(uint32_t, uint32_t, std::span<const uint8_t>)>;
  /// Returns the active frame's borrowed ImGui texture handle.
  using TextureFunction = std::function<void*()>;
  /// Releases the active frame's display texture.
  using ClearImageFunction = std::function<void()>;

  /// Persistence and file-dialog services supplied by the editor.
  struct WorkspaceServices {
    std::function<std::optional<std::filesystem::path>(bool isExr, bool save, const std::filesystem::path& initialPath)>
        ChoosePath;
    std::function<Engine::Result<Engine::SnapshotFrame>(const std::filesystem::path&)> ReadExr;
    std::function<Engine::Status(const Engine::SnapshotFrame&, const std::filesystem::path&)> WriteExr;
    std::function<Engine::Result<Engine::SnapshotPersistence::SnapshotLoadedWorkspace>(const std::filesystem::path&)>
        LoadWorkspace;
    std::function<Engine::Status(const std::filesystem::path&,
                                 const Engine::SnapshotPersistence::SnapshotWorkspaceDocument&)>
        SaveWorkspace;
  };

  /// Creates the workspace panel using the editor capture and texture services.
  SnapshotAnalysisPanel(BeginCaptureFunction beginCapture, ListCamerasFunction listCameras,
                        PollCaptureFunction pollCapture, CaptureStatusFunction getCaptureStatus,
                        CancelCaptureFunction cancelCapture, UploadImageFunction uploadImage,
                        TextureFunction getTexture, ClearImageFunction clearImage, WorkspaceServices workspaceServices);

  /// Draws capture controls, static frame tabs, inspectors, histograms, and comparisons.
  void Draw() override;
  /// Polls any asynchronous capture even when the workspace is not focused.
  void UpdateCaptureProgress() { UpdateCapture(); }
  /// Sets the callback used by the explicit exit control.
  /// @param handler Called when the user exits the focused workspace.
  void SetExitFocusedHandler(std::function<void()> handler) { m_ExitFocusedHandler = std::move(handler); }
  /// Requests the focused workspace to open and focuses its full-editor view.
  void Open() noexcept {
    m_SelectedCamera.reset();
    m_FocusRequested = true;
    m_DisplayDirty = true;
  }

private:
  struct Tab {
    std::string Name;
    Engine::SnapshotFrame Frame;
    std::filesystem::path ExrPath;
    std::optional<std::array<uint32_t, 2>> PinnedPixel;
    std::optional<std::array<uint32_t, 4>> Roi;
    Engine::SnapshotPlane SelectedPlane = Engine::SnapshotPlane::FinalColor;
    float HistogramMinimum = 0.0F;
    float HistogramMaximum = 1.0F;
    float Zoom = 1.0F;
    std::array<float, 2> Pan{};
  };

  void UpdateCapture();
  void RefreshCameraOptions();
  void StartCapture();
  void DrawFrame(Tab& tab, size_t tabIndex);
  void OpenExr();
  void OpenWorkspace();
  void SaveCurrentFrame();
  auto SaveWorkspaceAs() -> bool;
  auto BuildWorkspaceDocument(const std::filesystem::path& path) const
      -> Engine::SnapshotPersistence::SnapshotWorkspaceDocument;
  auto BuildDisplayPixels(const Tab& tab) const -> std::vector<uint8_t>;
  auto BuildHeatmapPixels(const Tab& tab) const -> std::vector<uint8_t>;
  auto BuildWipePixels(const Tab& tab) const -> std::vector<uint8_t>;
  void InvalidateDisplay() noexcept { m_DisplayDirty = true; }

  BeginCaptureFunction m_BeginCapture;
  ListCamerasFunction m_ListCameras;
  PollCaptureFunction m_PollCapture;
  CaptureStatusFunction m_GetCaptureStatus;
  CancelCaptureFunction m_CancelCapture;
  UploadImageFunction m_UploadImage;
  TextureFunction m_GetTexture;
  ClearImageFunction m_ClearImage;
  WorkspaceServices m_WorkspaceServices;
  std::function<void()> m_ExitFocusedHandler;
  std::vector<std::unique_ptr<Tab>> m_Tabs;
  std::optional<size_t> m_ActiveTab;
  std::vector<SnapshotAnalysisCameraOption> m_CameraOptions;
  std::optional<Engine::EntityHandle> m_SelectedCamera;
  std::optional<size_t> m_ComparisonReference;
  Engine::SnapshotAnalysis::DifferenceMetric m_ComparisonMetric = Engine::SnapshotAnalysis::DifferenceMetric::RgbRms;
  float m_HeatmapMinimum = 0.0F;
  float m_HeatmapMaximum = 1.0F;
  float m_WipePosition = 0.5F;
  std::optional<Engine::RenderSystem::SnapshotRenderTicket> m_CaptureTicket;
  std::string m_CaptureMessage;
  std::filesystem::path m_WorkspacePath;
  uint32_t m_WidthOverride = 0;
  uint32_t m_HeightOverride = 0;
  Engine::SnapshotPlaneMask m_SelectedCapturePlanes = DefaultSnapshotCapturePlanes;
  size_t m_UploadedTab = static_cast<size_t>(-1);
  Engine::SnapshotPlane m_UploadedPlane = Engine::SnapshotPlane::FinalColor;
  bool m_FocusRequested = true;
  bool m_DisplayDirty = true;
  bool m_IsolateMaterial = false;
  bool m_ShowHeatmap = false;
  bool m_ShowWipe = false;
  std::optional<std::array<uint32_t, 2>> m_RoiStart;
};

#pragma once

#include "panels/include/AnalyticalFrameArchive.h"
#include "panels/include/AnalyticalFrameViewModel.h"
#include "panels/include/BrdfAnalysisPanel.h"
#include "panels/include/EditorPanel.h"
#include "rendering/include/renderer/brdf/BrdfAnalysisSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

/// Static editor view for captured Analytical Frames and detached `.aframe` projects.
class AnalyticalFramePanel final : public EditorPanel {
public:
  /// File dialog purpose used to select a project or export destination.
  enum class FileOperation : uint8_t {
    OpenArchive,       ///< Opens a detached `.aframe` project.
    SaveArchive,       ///< Saves the displayed session as `.aframe`.
    ExportChart,       ///< Writes the analytic curve chart as PNG.
    ExportMeasurements ///< Writes measurements and settings as JSON.
  };

  /// A material target available for scene capture.
  struct Target {
    uint64_t EntityId = 0;                       ///< Document-local entity identifier containing the material.
    std::optional<size_t> MaterialOverrideIndex; ///< Submesh override slot, or empty for the entity material.
    std::string Label;                           ///< User-facing entity and material description.
  };

  /// An authored scene camera available for frame capture.
  struct CameraTarget {
    uint64_t EntityId = 0; ///< Document-local entity identifier of the camera.
    std::string Label;     ///< User-facing camera name.
  };

  /// Uploads display-ready, top-to-bottom RGBA8 bytes to the panel-owned texture.
  using UploadFunction = std::function<bool(uint32_t, uint32_t, std::span<const uint8_t>)>;
  /// Returns the current panel-owned texture handle borrowed for the UI frame.
  using TextureFunction = std::function<void*()>;
  /// Releases the panel-owned display texture when the view closes or the editor shuts down.
  using ClearTextureFunction = std::function<void()>;
  /// Chooses a path for the requested archive or export operation; empty means cancelled.
  using ChoosePathFunction =
      std::function<std::optional<std::filesystem::path>(FileOperation, const std::filesystem::path&)>;
  /// Supplies authored target materials in the active scene.
  using TargetProvider = std::function<Engine::Result<std::vector<Target>>()>;
  /// Supplies authored cameras. The editor may order its established default camera first.
  using CameraProvider = std::function<Engine::Result<std::vector<CameraTarget>>()>;
  /// Captures a copied scene snapshot for the selected material, authored camera, and local overrides.
  using SnapshotFunction = std::function<Engine::Result<Engine::Renderer::BrdfAnalysis::BrdfAnalysisSnapshot>(
      const Target&, const CameraTarget&, const Engine::Renderer::BrdfAnalysis::BrdfAnalysisOverrides&)>;
  /// Renders exactly one captured request on the editor's render-owning thread.
  using RenderFunction = std::function<Engine::Result<Engine::RenderOutput>(
      const Engine::Renderer::BrdfAnalysis::BrdfAnalysisRenderRequest&)>;

  /// Creates the capture controller and static viewer.
  /// @param targets Supplies the material targets from the active authored scene.
  /// @param cameras Supplies authored scene cameras with the preferred camera first.
  /// @param captureSnapshot Copies the selected scene/material/camera state and applies frame-local overrides.
  /// @param render Renders one request per editor frame on the render-owning thread.
  /// @param upload Uploads display-ready CPU pixels to the viewer texture.
  /// @param texture Returns the viewer texture handle for the current UI frame.
  /// @param clearTexture Releases the viewer texture when the panel closes.
  /// @param choosePath Selects an archive or export path, or returns empty on cancellation.
  AnalyticalFramePanel(TargetProvider targets, CameraProvider cameras, SnapshotFunction captureSnapshot,
                       RenderFunction render, UploadFunction upload, TextureFunction texture,
                       ClearTextureFunction clearTexture, ChoosePathFunction choosePath);

  /// Replaces the displayed complete session and restores its typed viewport state.
  void SetSession(Editor::Panels::AnalyticalFrameSession session);
  /// Opens the static viewer window.
  void Open() noexcept { m_Open = true; }
  /// Closes the static viewer and releases its display texture while retaining the session.
  void Close();
  /// Draws a static image viewer, comparison controls, pixel inspection and measurements.
  void Draw() override;
  /// Handles archive actions locally; it does not interact with or replace the active scene.
  void OnEvent(Engine::Events::Event& event) override;

  /// Gets the detached session currently displayed, if any.
  [[nodiscard]] auto GetSession() const noexcept -> const std::optional<Editor::Panels::AnalyticalFrameSession>& {
    return m_Session;
  }

private:
  auto FindVariant(Engine::Renderer::BrdfModel model) const -> const Editor::Panels::AnalyticalFrameVariant*;
  auto IsRelativeErrorCompatible() const -> bool;
  auto BuildCompletedComparison() const -> std::optional<BrdfAnalysisPanel::CompletedComparison>;
  void RefreshTargets();
  void StartCapture();
  void AdvanceCapture();
  void CancelCapture();
  void DrawCaptureControls();
  void CompleteCapture();
  auto BuildCurrentImage() const -> Engine::Result<std::vector<uint8_t>>;
  bool RefreshImage();
  void DrawControls();
  void DrawMeasurements() const;
  void DrawCurveChart() const;
  void DrawCanvas();
  void UpdatePinnedPixel(float mouseX, float mouseY, float imageX, float imageY, float scale);
  void StoreViewportState();
  void OpenArchive();
  void SaveArchive();
  void Export(bool chart);

  UploadFunction m_Upload;
  TextureFunction m_Texture;
  ClearTextureFunction m_ClearTexture;
  ChoosePathFunction m_ChoosePath;
  TargetProvider m_GetTargets;
  CameraProvider m_GetCameras;
  SnapshotFunction m_CreateSnapshot;
  RenderFunction m_Render;
  struct ModelSelection {
    Engine::Renderer::BrdfModel Model;
    bool Selected = true;
  };
  std::vector<Target> m_Targets;
  std::vector<CameraTarget> m_Cameras;
  size_t m_SelectedTarget = 0;
  size_t m_SelectedCamera = 0;
  bool m_TargetsDirty = true;
  bool m_OverrideResolution = false;
  uint32_t m_OverrideWidth = 1920;
  uint32_t m_OverrideHeight = 1080;
  bool m_OverrideRoughness = false;
  float m_Roughness = 0.5F;
  bool m_OverrideGtrGamma = false;
  float m_GtrGamma = 2.0F;
  std::vector<ModelSelection> m_ModelSelections;
  std::optional<Engine::Renderer::BrdfAnalysis::BrdfAnalysisSnapshot> m_CapturedSnapshot;
  std::optional<Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSession> m_Capture;
  std::string m_CapturedTargetLabel;
  std::optional<Editor::Panels::AnalyticalFrameSession> m_Session;
  Editor::Panels::AnalyticalFrameViewState m_ViewState;
  std::string m_Status = "Open an .aframe project or capture an Analytical Frame from a scene.";
  uint32_t m_ImageWidth = 0;
  uint32_t m_ImageHeight = 0;
  bool m_ImageDirty = true;
  bool m_Open = true;
};

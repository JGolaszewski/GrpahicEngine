#pragma once

#include "panels/include/EditorPanel.h"
#include "rendering/include/RenderOutput.h"
#include "rendering/include/RenderOutputExportService.h"
#include "scene/include/ArchetypeWorld.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

/// On-demand full-resolution viewer for a selected scene camera.
class CameraPreviewPanel final : public EditorPanel {
public:
  /// Renders the specified scene entity through its authored scene camera.
  using RenderFunction = std::function<Engine::Result<Engine::RenderOutput>(Engine::EntityHandle)>;
  /// Uploads display-ready top-to-bottom RGBA8 pixels into the viewer-owned texture.
  using PreviewUploadFunction = std::function<bool(uint32_t, uint32_t, std::span<const uint8_t>)>;
  /// Returns the viewer-owned texture handle borrowed for the duration of the UI frame.
  using TextureFunction = std::function<void*()>;
  /// Releases the viewer-owned image after the user closes the window or rendering fails.
  using ClearPreviewFunction = std::function<void()>;
  /// Opens a native PNG or EXR save dialog; a missing path means cancellation.
  using ChooseExportPathFunction =
      std::function<std::optional<std::filesystem::path>(bool, const std::filesystem::path&)>;
  /// Starts an async export for the camera entity, resolving its current scene state in the editor.
  using StartExportFunction = std::function<Engine::Result<Engine::RenderOutputExportService::RequestId>(
      Engine::EntityHandle, bool, std::vector<Engine::RenderAov>, std::filesystem::path)>;
  /// Returns the latest async export status for a request.
  using ExportStatusFunction =
      std::function<std::optional<Engine::RenderOutputExportStatus>(Engine::RenderOutputExportService::RequestId)>;

  CameraPreviewPanel(RenderFunction render, PreviewUploadFunction uploadPreview, TextureFunction getTexture,
                     ClearPreviewFunction clearPreview, ChooseExportPathFunction chooseExportPath,
                     StartExportFunction startExport, ExportStatusFunction getExportStatus);

  void Draw() override;
  void OnEvent(Engine::Events::Event& event) override;
  void Close();

private:
  bool RenderSelectedCamera();
  void StartExport(bool exr);
  void UpdateExportStatus();

  RenderFunction m_Render;
  PreviewUploadFunction m_UploadPreview;
  TextureFunction m_GetTexture;
  ClearPreviewFunction m_ClearPreview;
  ChooseExportPathFunction m_ChooseExportPath;
  StartExportFunction m_StartExport;
  ExportStatusFunction m_GetExportStatus;
  Engine::EntityHandle m_Camera = Engine::InvalidEntityHandle;
  uint32_t m_ImageWidth = 0;
  uint32_t m_ImageHeight = 0;
  std::string m_Status = "Click a selected camera preview to open the viewer.";
  std::string m_ExportMessage;
  std::string m_ExportFormat;
  std::optional<Engine::RenderOutputExportService::RequestId> m_ExportRequest;
  std::array<bool, 5> m_SelectedAovs = {true, true, true, true, true};
  float m_Zoom = 1.0F;
  bool m_Open = false;
  bool m_RefreshRequested = false;
  bool m_HasImage = false;
};

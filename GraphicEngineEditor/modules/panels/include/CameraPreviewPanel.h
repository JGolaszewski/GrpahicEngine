#pragma once

#include "panels/include/EditorPanel.h"
#include "rendering/include/RenderOutput.h"
#include "scene/include/ArchetypeWorld.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>

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

  CameraPreviewPanel(RenderFunction render, PreviewUploadFunction uploadPreview, TextureFunction getTexture,
                     ClearPreviewFunction clearPreview);

  void Draw() override;
  void OnEvent(Engine::Events::Event& event) override;
  void Close();

private:
  bool RenderSelectedCamera();

  RenderFunction m_Render;
  PreviewUploadFunction m_UploadPreview;
  TextureFunction m_GetTexture;
  ClearPreviewFunction m_ClearPreview;
  Engine::EntityHandle m_Camera = Engine::InvalidEntityHandle;
  uint32_t m_ImageWidth = 0;
  uint32_t m_ImageHeight = 0;
  std::string m_Status = "Click a selected camera preview to open the viewer.";
  float m_Zoom = 1.0F;
  bool m_Open = false;
  bool m_RefreshRequested = false;
  bool m_HasImage = false;
};

#pragma once

#include "panels/include/EditorPanel.h"
#include "rendering/include/RenderOutput.h"
#include "rendering/include/renderer/CameraView.h"
#include "scene/include/Scene.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

/// Editor workflow for selecting an authored scene camera, previewing a final
/// render, and exporting PNG or selected AOVs as multipart EXR.
class RenderPanel final : public EditorPanel {
public:
  using RenderFunction =
      std::function<Engine::Result<Engine::RenderOutput>(const Engine::Renderer::CameraView&, uint32_t, uint32_t)>;
  using PreviewFunction = std::function<bool(uint32_t, uint32_t, std::span<const uint8_t>)>;

  RenderPanel(RenderFunction render, PreviewFunction setPreview, std::function<void*()> getPreviewTexture);
  void SetScene(std::shared_ptr<Engine::Scene> scene);
  void Open() noexcept { m_Open = true; }
  void Draw() override;

private:
  struct CameraChoice {
    Engine::EntityHandle Entity = Engine::InvalidEntityHandle;
    std::string Label;
    Engine::Renderer::CameraView View;
    uint32_t Width = 0;
    uint32_t Height = 0;
  };

  std::vector<CameraChoice> GetCameras() const;
  auto FindSelected(const std::vector<CameraChoice>& cameras) const -> const CameraChoice*;
  bool RenderSelected(const CameraChoice& camera);
  bool UpdatePreviewTexture(const Engine::RenderOutput& output);
  void SavePng(const CameraChoice& camera);
  void SaveExr(const CameraChoice& camera);

  std::shared_ptr<Engine::Scene> m_Scene;
  RenderFunction m_Render;
  PreviewFunction m_SetPreview;
  std::function<void*()> m_GetPreviewTexture;
  Engine::EntityHandle m_SelectedCamera = Engine::InvalidEntityHandle;
  std::array<bool, 5> m_SelectedAovs = {true, true, true, true, true};
  std::unique_ptr<Engine::RenderOutput> m_LastOutput;
  std::string m_Status = "Select a scene camera to begin.";
  bool m_Open = true;
};

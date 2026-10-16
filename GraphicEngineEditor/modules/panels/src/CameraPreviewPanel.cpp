#include "panels/include/CameraPreviewPanel.h"

#include "events/include/EditorEvents.h"
#include "panels/include/CameraPreviewImageLayout.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <vector>

namespace {

auto EncodeChannel(float sample) -> uint8_t {
  const float safe = std::isfinite(sample) ? (std::max)(sample, 0.0F) : 0.0F;
  const float mapped = safe / (1.0F + safe);
  const float srgb = mapped <= 0.0031308F ? mapped * 12.92F : 1.055F * std::pow(mapped, 1.0F / 2.4F) - 0.055F;
  return static_cast<uint8_t>(std::lround(std::clamp(srgb, 0.0F, 1.0F) * 255.0F));
}

auto ConvertFinalColorToRgba8(const Engine::RenderOutput& output) -> std::vector<uint8_t> {
  const auto pixels = output.GetPixels(Engine::RenderAov::FinalColor);
  const size_t expected = static_cast<size_t>(output.Width) * output.Height * 4U;
  if (output.Width == 0 || output.Height == 0 || pixels.size() != expected) {
    return {};
  }
  std::vector<uint8_t> rgba(expected);
  for (size_t index = 0; index < expected; index += 4U) {
    rgba[index] = EncodeChannel(pixels[index]);
    rgba[index + 1U] = EncodeChannel(pixels[index + 1U]);
    rgba[index + 2U] = EncodeChannel(pixels[index + 2U]);
    const float alpha = std::isfinite(pixels[index + 3U]) ? std::clamp(pixels[index + 3U], 0.0F, 1.0F) : 0.0F;
    rgba[index + 3U] = static_cast<uint8_t>(std::lround(alpha * 255.0F));
  }
  return rgba;
}

} // namespace

CameraPreviewPanel::CameraPreviewPanel(RenderFunction render, PreviewUploadFunction uploadPreview,
                                       TextureFunction getTexture, ClearPreviewFunction clearPreview,
                                       ChooseExportPathFunction chooseExportPath, StartExportFunction startExport,
                                       ExportStatusFunction getExportStatus)
    : m_Render(std::move(render)), m_UploadPreview(std::move(uploadPreview)), m_GetTexture(std::move(getTexture)),
      m_ClearPreview(std::move(clearPreview)), m_ChooseExportPath(std::move(chooseExportPath)),
      m_StartExport(std::move(startExport)), m_GetExportStatus(std::move(getExportStatus)) {}

void CameraPreviewPanel::OnEvent(Engine::Events::Event& event) {
  Engine::Events::EventDispatcher dispatcher(event);
  dispatcher.Dispatch<CameraPreviewOpenRequestedEvent>([this](const auto& request) -> bool {
    m_Camera = request.GetEntity();
    m_Open = true;
    m_RefreshRequested = true;
    m_HasImage = false;
    m_Zoom = 1.0F;
    m_ImageOffset = {};
    m_IsPanning = false;
    m_Status = "Rendering camera preview...";
    return true;
  });
}

void CameraPreviewPanel::Close() {
  m_Open = false;
  m_RefreshRequested = false;
  m_HasImage = false;
  m_ImageWidth = 0;
  m_ImageHeight = 0;
  m_Camera = Engine::InvalidEntityHandle;
  if (m_ClearPreview) {
    m_ClearPreview();
  }
}

bool CameraPreviewPanel::RenderSelectedCamera() {
  if (m_Camera == Engine::InvalidEntityHandle || !m_Render) {
    m_HasImage = false;
    m_ImageWidth = 0;
    m_ImageHeight = 0;
    m_Status = "The selected camera is no longer available.";
    if (m_ClearPreview) {
      m_ClearPreview();
    }
    return false;
  }
  auto result = m_Render(m_Camera);
  if (!result) {
    m_HasImage = false;
    m_ImageWidth = 0;
    m_ImageHeight = 0;
    m_Status = "Preview render failed: " + result.error().Message;
    if (m_ClearPreview) {
      m_ClearPreview();
    }
    return false;
  }
  const auto& output = *result;
  std::vector<uint8_t> rgba = ConvertFinalColorToRgba8(output);
  if (rgba.empty() || !m_UploadPreview || !m_UploadPreview(output.Width, output.Height, rgba)) {
    m_HasImage = false;
    m_ImageWidth = 0;
    m_ImageHeight = 0;
    m_Status = "Render completed, but the editor could not display the preview image.";
    if (m_ClearPreview) {
      m_ClearPreview();
    }
    return false;
  }
  m_ImageWidth = output.Width;
  m_ImageHeight = output.Height;
  m_HasImage = true;
  m_Zoom = 1.0F;
  m_Status = "Rendered " + std::to_string(m_ImageWidth) + " x " + std::to_string(m_ImageHeight) + " preview.";
  return true;
}

void CameraPreviewPanel::StartExport(bool exr) {
  m_ExportRequest.reset();
  const std::filesystem::path defaultPath =
      std::filesystem::current_path() / (exr ? "camera-preview.exr" : "camera-preview.png");
  if (!m_ChooseExportPath || !m_StartExport) {
    m_ExportMessage = "Camera export is unavailable.";
    return;
  }
  std::vector<Engine::RenderAov> selectedAovs;
  constexpr std::array<Engine::RenderAov, 5> aovs = {
      Engine::RenderAov::FinalColor, Engine::RenderAov::Depth, Engine::RenderAov::WorldNormal,
      Engine::RenderAov::SpecularLighting, Engine::RenderAov::DiffuseLighting};
  if (exr) {
    for (size_t index = 0; index < aovs.size(); ++index) {
      if (m_SelectedAovs[index]) {
        selectedAovs.push_back(aovs[index]);
      }
    }
    if (selectedAovs.empty()) {
      m_ExportMessage = "Select at least one AOV before saving EXR.";
      return;
    }
  }

  auto path = m_ChooseExportPath(exr, defaultPath);
  if (!path) {
    m_ExportMessage = exr ? "EXR export cancelled." : "PNG export cancelled.";
    return;
  }

  auto request = m_StartExport(m_Camera, exr, std::move(selectedAovs), std::move(*path));
  if (!request) {
    m_ExportRequest.reset();
    m_ExportMessage = std::string(exr ? "EXR" : "PNG") + " export failed to start: " + request.error().Message;
    return;
  }
  m_ExportRequest = *request;
  m_ExportFormat = exr ? "EXR" : "PNG";
  m_ExportMessage = m_ExportFormat + " export started; rendering camera...";
}

void CameraPreviewPanel::UpdateExportStatus() {
  if (!m_ExportRequest || !m_GetExportStatus) {
    return;
  }
  const auto status = m_GetExportStatus(*m_ExportRequest);
  if (!status) {
    m_ExportMessage = "Export status is unavailable.";
    m_ExportRequest.reset();
    return;
  }
  switch (status->Phase) {
  case Engine::RenderOutputExportPhase::WaitingForReadback:
    m_ExportMessage = m_ExportFormat + " export: waiting for camera render and GPU readback...";
    break;
  case Engine::RenderOutputExportPhase::Writing:
    m_ExportMessage = m_ExportFormat + " export: writing image file...";
    break;
  case Engine::RenderOutputExportPhase::Completed:
  case Engine::RenderOutputExportPhase::Failed:
    m_ExportMessage = status->Message;
    m_ExportRequest.reset();
    break;
  }
}

void CameraPreviewPanel::Draw() {
  if (!m_Open) {
    return;
  }
  UpdateExportStatus();
  if (m_RefreshRequested) {
    m_RefreshRequested = false;
    (void)RenderSelectedCamera();
  }

  const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
  const ImVec2 displaySize = mainViewport == nullptr ? ImVec2(1280.0F, 720.0F) : mainViewport->WorkSize;
  ImGui::SetNextWindowSize(ImVec2(displaySize.x * 0.78F, displaySize.y * 0.78F), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(320.0F, 240.0F), ImVec2(displaySize.x, displaySize.y));
  if (ImGui::Begin("Preview", &m_Open, ImGuiWindowFlags_NoCollapse)) {
    if (ImGui::Button("Refresh")) {
      (void)RenderSelectedCamera();
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit")) {
      m_Zoom = 1.0F;
      m_ImageOffset = {};
    }
    ImGui::SameLine();
    if (ImGui::Button("1:1")) {
      const ImVec2 available = ImGui::GetContentRegionAvail();
      const float fitScale =
          CameraPreviewImageLayout::ComputeFitScale(m_ImageWidth, m_ImageHeight, available.x, available.y);
      if (fitScale > 0.0F) {
        m_Zoom = CameraPreviewImageLayout::ClampZoom(1.0F / fitScale);
        m_ImageOffset = {};
      }
    }
    ImGui::SameLine();
    ImGui::Text("%u x %u | %.0f%%", m_ImageWidth, m_ImageHeight, m_Zoom * 100.0F);
    ImGui::TextWrapped("%s", m_Status.c_str());
    ImGui::Separator();

    const bool exportBusy =
        m_ExportRequest.has_value() && m_GetExportStatus && ([this] {
          const auto current = m_GetExportStatus(*m_ExportRequest);
          return current && (current->Phase == Engine::RenderOutputExportPhase::WaitingForReadback ||
                             current->Phase == Engine::RenderOutputExportPhase::Writing);
        })();
    if (exportBusy) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Save PNG...")) {
      StartExport(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save EXR...")) {
      StartExport(true);
    }
    if (exportBusy) {
      ImGui::EndDisabled();
    }
    ImGui::SeparatorText("EXR AOVs");
    constexpr std::array<Engine::RenderAov, 5> aovs = {
        Engine::RenderAov::FinalColor, Engine::RenderAov::Depth, Engine::RenderAov::WorldNormal,
        Engine::RenderAov::SpecularLighting, Engine::RenderAov::DiffuseLighting};
    for (size_t index = 0; index < aovs.size(); ++index) {
      ImGui::Checkbox(Engine::RenderOutput::GetDescriptor(aovs[index]).Name, &m_SelectedAovs[index]);
      if (index == 1U) {
        ImGui::NewLine();
      } else if (index + 1U < aovs.size()) {
        ImGui::SameLine();
      }
    }
    if (!m_ExportMessage.empty()) {
      ImGui::TextWrapped("%s", m_ExportMessage.c_str());
    }
    if (m_ExportRequest && m_GetExportStatus) {
      const auto current = m_GetExportStatus(*m_ExportRequest);
      if (current && (current->Phase == Engine::RenderOutputExportPhase::WaitingForReadback ||
                      current->Phase == Engine::RenderOutputExportPhase::Writing)) {
        ImGui::ProgressBar(current->Progress, ImVec2(-1.0F, 0.0F));
      }
    }
    ImGui::Separator();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (ImGui::BeginChild("CameraPreviewImage", available, true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
      const ImVec2 childAvailable = ImGui::GetContentRegionAvail();
      const CameraPreviewImageLayout::ImageSize imageSize = CameraPreviewImageLayout::ComputeImageSize(
          m_ImageWidth, m_ImageHeight, childAvailable.x, childAvailable.y, m_Zoom);
      if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0F && m_HasImage) {
        m_Zoom = CameraPreviewImageLayout::ClampZoom(m_Zoom * std::pow(1.12F, ImGui::GetIO().MouseWheel));
      }
      if (m_HasImage && imageSize.Width > 0.0F && imageSize.Height > 0.0F) {
        ImVec2 cursor = ImGui::GetCursorPos();
        if (imageSize.Width < childAvailable.x) {
          cursor.x += (childAvailable.x - imageSize.Width) * 0.5F + m_ImageOffset[0];
        } else {
          cursor.x += m_ImageOffset[0];
        }
        if (imageSize.Height < childAvailable.y) {
          cursor.y += (childAvailable.y - imageSize.Height) * 0.5F + m_ImageOffset[1];
        } else {
          cursor.y += m_ImageOffset[1];
        }
        ImGui::SetCursorPos(cursor);
        void* texture = m_GetTexture ? m_GetTexture() : nullptr;
        if (texture != nullptr) {
          ImGui::Image(texture, ImVec2(imageSize.Width, imageSize.Height));
          if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
              m_IsPanning = true;
              const ImVec2 mouse = ImGui::GetIO().MousePos;
              m_LastMousePosition = {mouse.x, mouse.y};
            }
          }
        } else {
          ImGui::TextDisabled("Preview texture is unavailable.");
        }
        if (m_IsPanning) {
          ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
          if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            m_ImageOffset[0] += mouse.x - m_LastMousePosition[0];
            m_ImageOffset[1] += mouse.y - m_LastMousePosition[1];
            m_LastMousePosition = {mouse.x, mouse.y};
          }
          if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_IsPanning = false;
          }
        }
      } else if (!m_HasImage) {
        ImGui::TextDisabled("No full-resolution preview is available.");
      }
    }
    ImGui::EndChild();
    ImGui::TextDisabled("Drag the image to pan; hover over it and use the mouse wheel to zoom.");
  }
  ImGui::End();
  if (!m_Open && m_ClearPreview) {
    m_ClearPreview();
    m_HasImage = false;
    m_ImageWidth = 0;
    m_ImageHeight = 0;
  }
}

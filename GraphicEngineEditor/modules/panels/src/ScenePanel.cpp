#include "panels/include/ScenePanel.h"

#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>

// ================================================================================
// ScenePanel::OnEvent
// ================================================================================
// Receives ViewportChangedEvent to update the texture ID and size that we
// render in Draw(). Input events are intentionally not handled here — the
// Editor's fallback layer manages camera controls.
// ================================================================================

void ScenePanel::OnEvent(Engine::Events::Event& event) {
  Engine::Events::EventDispatcher dispatcher(event);

  dispatcher.Dispatch<ViewportChangedEvent>([this](const auto& eventData) -> auto {
    m_TextureId = eventData.GetTextureId();
    m_ViewportSize = {eventData.GetWidth(), eventData.GetHeight()};
    return false;
  });
  dispatcher.Dispatch<CameraPreviewChangedEvent>([this](const auto& eventData) -> auto {
    m_CameraPreviewEntity = eventData.GetEntity();
    m_CameraPreviewTextureId = eventData.GetTextureId();
    m_CameraPreviewSize = {eventData.GetWidth(), eventData.GetHeight()};
    return false;
  });
}

void ScenePanel::Draw() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));

  ImGuiWindowClass viewportClass;
  viewportClass.ClassId = ImGui::GetID("ViewportClass");
  viewportClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
  viewportClass.DockingAllowUnclassed = false;
  ImGui::SetNextWindowClass(&viewportClass);

  ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);

  bool isHovered = ImGui::IsWindowHovered();
  if (isHovered != m_IsHovered) {
    m_IsHovered = isHovered;
    EmitEvent(std::make_unique<ViewportHoveredEvent>(isHovered));
  }

  const ImVec2 availableSize = ImGui::GetContentRegionAvail();
  if (availableSize.x > 0.0F && availableSize.y > 0.0F) {
    const uint32_t newWidth = static_cast<uint32_t>(availableSize.x);
    const uint32_t newHeight = static_cast<uint32_t>(availableSize.y);
    if (newWidth != static_cast<uint32_t>(m_ViewportSize.first) ||
        newHeight != static_cast<uint32_t>(m_ViewportSize.second)) {
      EmitEvent(std::make_unique<ViewportResizeRequestedEvent>(newWidth, newHeight));
    }
  }

  const ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
  const ImVec2 viewportImagePosition = ImGui::GetCursorScreenPos();
  ImVec2 previewSize(0.0F, 0.0F);
  ImVec2 previewPosition(viewportImagePosition.x + 8.0F, viewportImagePosition.y + 8.0F);
  bool hasCameraPreview =
      m_CameraPreviewTextureId != nullptr && m_CameraPreviewSize.first > 0.0F && m_CameraPreviewSize.second > 0.0F;
  if (hasCameraPreview) {
    constexpr float previewMaxEdge = 320.0F;
    constexpr float viewportFraction = 0.35F;
    const float aspect = m_CameraPreviewSize.first / m_CameraPreviewSize.second;
    const float maxWidth = (std::min)(previewMaxEdge, viewportPanelSize.x * viewportFraction);
    const float maxHeight = (std::min)(previewMaxEdge, viewportPanelSize.y * viewportFraction);
    previewSize = ImVec2(maxWidth, maxWidth / aspect);
    if (previewSize.y > maxHeight) {
      previewSize.y = maxHeight;
      previewSize.x = maxHeight * aspect;
    }
    hasCameraPreview = previewSize.x > 0.0F && previewSize.y > 0.0F;
  }
  bool viewportImageClicked = false;
  ImVec2 viewportMousePosition(0.0F, 0.0F);
  if (m_TextureId != nullptr) {
    ImGui::Image(m_TextureId, viewportPanelSize);

    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      viewportMousePosition = ImGui::GetIO().MousePos;
      const float viewportX = viewportMousePosition.x - viewportImagePosition.x;
      const float viewportY = viewportMousePosition.y - viewportImagePosition.y;
      if (viewportX >= 0.0F && viewportY >= 0.0F && viewportX < viewportPanelSize.x &&
          viewportY < viewportPanelSize.y) {
        viewportImageClicked = true;
      }
    }
  } else {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::TextDisabled("Viewport not available");
  }

  if (hasCameraPreview) {
    ImGui::SetCursorScreenPos(previewPosition);
    ImGui::Image(m_CameraPreviewTextureId, previewSize);
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      EmitEvent(std::make_unique<CameraPreviewOpenRequestedEvent>(m_CameraPreviewEntity));
    }
  }
  if (viewportImageClicked) {
    const bool clickOverPreview = hasCameraPreview && viewportMousePosition.x >= previewPosition.x &&
                                  viewportMousePosition.y >= previewPosition.y &&
                                  viewportMousePosition.x < previewPosition.x + previewSize.x &&
                                  viewportMousePosition.y < previewPosition.y + previewSize.y;
    if (!clickOverPreview) {
      EmitEvent(std::make_unique<ViewportClickedEvent>(viewportMousePosition.x - viewportImagePosition.x,
                                                       viewportMousePosition.y - viewportImagePosition.y));
    }
  }

  ImGui::End();
  ImGui::PopStyleVar();
}

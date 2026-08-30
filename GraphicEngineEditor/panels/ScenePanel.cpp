#include "ScenePanel.h"

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

    dispatcher.Dispatch<ViewportChangedEvent>([this](const auto& e) {
        m_TextureId = e.GetTextureId();
        m_ViewportSize = { e.GetWidth(), e.GetHeight() };
        return false;
    });
}

void ScenePanel::Draw() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

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
    if (availableSize.x > 0.0f && availableSize.y > 0.0f) {
        const uint32_t newWidth = static_cast<uint32_t>(availableSize.x);
        const uint32_t newHeight = static_cast<uint32_t>(availableSize.y);
        if (newWidth != static_cast<uint32_t>(m_ViewportSize.first) ||
            newHeight != static_cast<uint32_t>(m_ViewportSize.second)) {
            EmitEvent(std::make_unique<ViewportResizeRequestedEvent>(newWidth, newHeight));
        }
    }

    const ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    const ImVec2 viewportImagePosition = ImGui::GetCursorScreenPos();
    if (m_TextureId) {
        ImGui::Image(m_TextureId, viewportPanelSize);

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 mousePosition = ImGui::GetIO().MousePos;
            const float viewportX = mousePosition.x - viewportImagePosition.x;
            const float viewportY = mousePosition.y - viewportImagePosition.y;
            if (viewportX >= 0.0f && viewportY >= 0.0f &&
                viewportX < viewportPanelSize.x && viewportY < viewportPanelSize.y) {
                EmitEvent(std::make_unique<ViewportClickedEvent>(viewportX, viewportY));
            }
        }
    }
    else {
        ImGui::TextDisabled("Viewport not available");
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

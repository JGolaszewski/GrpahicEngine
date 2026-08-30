#include "ScenePanel.h"

#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

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

void ScenePanel::SetLightGizmos(std::vector<LightGizmo> gizmos) {
    m_LightGizmos = std::move(gizmos);
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
    }
    else {
        ImGui::TextDisabled("Viewport not available");
    }

    if (m_TextureId && viewportPanelSize.x > 0.0f && viewportPanelSize.y > 0.0f) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 mousePosition = ImGui::GetIO().MousePos;
        int hoveredGizmo = -1;
        float closestDistanceSquared = 0.0f;

        for (size_t i = 0; i < m_LightGizmos.size(); ++i) {
            const LightGizmo& gizmo = m_LightGizmos[i];
            const ImVec2 markerPosition(
                viewportImagePosition.x + gizmo.normalizedPosition.x * viewportPanelSize.x,
                viewportImagePosition.y + gizmo.normalizedPosition.y * viewportPanelSize.y);
            const float dx = mousePosition.x - markerPosition.x;
            const float dy = mousePosition.y - markerPosition.y;
            const float distanceSquared = dx * dx + dy * dy;
            const float hitRadius = gizmo.selected ? 12.0f : 10.0f;
            if (distanceSquared <= hitRadius * hitRadius &&
                (hoveredGizmo < 0 || distanceSquared < closestDistanceSquared)) {
                hoveredGizmo = static_cast<int>(i);
                closestDistanceSquared = distanceSquared;
            }

            const ImVec4 color(
                std::clamp(gizmo.color.x, 0.0f, 1.0f),
                std::clamp(gizmo.color.y, 0.0f, 1.0f),
                std::clamp(gizmo.color.z, 0.0f, 1.0f),
                1.0f);
            const ImU32 markerColor = ImGui::ColorConvertFloat4ToU32(color);
            const ImU32 outlineColor = gizmo.selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 20, 255);
            const float markerRadius = gizmo.selected ? 8.0f : 6.0f;

            drawList->AddCircleFilled(markerPosition, markerRadius, markerColor);
            drawList->AddCircle(markerPosition, markerRadius + 2.0f, outlineColor, 16, 2.0f);
            if (gizmo.selected) {
                const std::string label = "Light " + std::to_string(gizmo.index);
                drawList->AddText(ImVec2(markerPosition.x + 12.0f, markerPosition.y - 8.0f), outlineColor, label.c_str());
            }
        }

        if (hoveredGizmo >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const LightGizmo& gizmo = m_LightGizmos[static_cast<size_t>(hoveredGizmo)];
            EmitEvent(std::make_unique<EntitySelectedEvent>(
                EntitySelectedEvent::ItemType::Light,
                -1, gizmo.index, -1,
                Engine::InvalidEntityId, gizmo.id, Engine::InvalidCameraId));
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

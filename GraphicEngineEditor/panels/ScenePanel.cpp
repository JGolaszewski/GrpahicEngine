#include "ScenePanel.h"

#include <algorithm>
#include <cmath>
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

            if (gizmo.hasCone) {
                const ImVec2 directionEnd(
                    viewportImagePosition.x + gizmo.normalizedDirectionEnd.x * viewportPanelSize.x,
                    viewportImagePosition.y + gizmo.normalizedDirectionEnd.y * viewportPanelSize.y);
                const ImVec2 coneLeft(
                    viewportImagePosition.x + gizmo.normalizedConeLeft.x * viewportPanelSize.x,
                    viewportImagePosition.y + gizmo.normalizedConeLeft.y * viewportPanelSize.y);
                const ImVec2 coneRight(
                    viewportImagePosition.x + gizmo.normalizedConeRight.x * viewportPanelSize.x,
                    viewportImagePosition.y + gizmo.normalizedConeRight.y * viewportPanelSize.y);
                const ImU32 coneFillColor = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(color.x, color.y, color.z, gizmo.selected ? 0.16f : 0.09f));
                const ImU32 coneLineColor = gizmo.selected ? IM_COL32(255, 255, 255, 235) :
                    ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 0.95f));
                const float lineThickness = gizmo.selected ? 2.5f : 2.0f;

                drawList->AddTriangleFilled(markerPosition, coneLeft, coneRight, coneFillColor);
                drawList->AddLine(markerPosition, coneLeft, coneLineColor, lineThickness);
                drawList->AddLine(markerPosition, coneRight, coneLineColor, lineThickness);
                drawList->AddLine(coneLeft, coneRight, coneLineColor, lineThickness);
                drawList->AddLine(markerPosition, directionEnd, coneLineColor, lineThickness);

                const float axisX = directionEnd.x - markerPosition.x;
                const float axisY = directionEnd.y - markerPosition.y;
                const float axisLength = std::sqrt(axisX * axisX + axisY * axisY);
                if (axisLength > 1.0f) {
                    const float axisUnitX = axisX / axisLength;
                    const float axisUnitY = axisY / axisLength;
                    const float perpendicularX = -axisUnitY;
                    const float perpendicularY = axisUnitX;
                    const ImVec2 arrowLeft(
                        directionEnd.x - axisUnitX * 10.0f + perpendicularX * 5.0f,
                        directionEnd.y - axisUnitY * 10.0f + perpendicularY * 5.0f);
                    const ImVec2 arrowRight(
                        directionEnd.x - axisUnitX * 10.0f - perpendicularX * 5.0f,
                        directionEnd.y - axisUnitY * 10.0f - perpendicularY * 5.0f);
                    drawList->AddLine(directionEnd, arrowLeft, coneLineColor, lineThickness);
                    drawList->AddLine(directionEnd, arrowRight, coneLineColor, lineThickness);
                }
            }

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

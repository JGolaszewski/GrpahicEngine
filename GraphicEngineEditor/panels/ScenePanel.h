#pragma once

#include "EditorPanel.h"
#include "events/EditorEvents.h"
#include "Scene.h"
#include <DirectXMath.h>
#include <cstdint>
#include <vector>
#include <utility>

// ================================================================================
// ScenePanel.h
// ================================================================================
// Decoupled from the Editor. Receives ViewportChangedEvent to know the
// current viewport texture ID and size. Emits ViewportResizeRequestedEvent
// when the ImGui window size changes, and ViewportHoveredEvent when the
// hover state changes.
// ================================================================================

class ScenePanel : public EditorPanel {
public:
    struct LightGizmo {
        Engine::LightId id = Engine::InvalidLightId;
        int index = -1;
        DirectX::XMFLOAT2 normalizedPosition = { 0.0f, 0.0f };
        DirectX::XMFLOAT2 normalizedDirectionEnd = { 0.0f, 0.0f };
        DirectX::XMFLOAT2 normalizedConeLeft = { 0.0f, 0.0f };
        DirectX::XMFLOAT2 normalizedConeRight = { 0.0f, 0.0f };
        DirectX::XMFLOAT3 color = { 1.0f, 1.0f, 1.0f };
        bool hasCone = false;
        bool selected = false;
    };

    ScenePanel() = default;

    void Draw() override;
    void OnEvent(Engine::Events::Event& event) override;
    void SetLightGizmos(std::vector<LightGizmo> gizmos);

private:
    void* m_TextureId = nullptr;
    std::pair<float, float> m_ViewportSize = { 0.0f, 0.0f };
    std::vector<LightGizmo> m_LightGizmos;
    bool m_IsHovered = false;
};

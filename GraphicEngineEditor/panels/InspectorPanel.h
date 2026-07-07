#pragma once

#include "EditorPanel.h"
#include "events/EditorEvents.h"
#include <DirectXMath.h>
#include <memory>

namespace Renderer {
    class Entity;
    class SpotLight;
    class Camera;
}

// ================================================================================
// InspectorPanel.h
// ================================================================================
// Decoupled from the Editor. Receives EntitySelectedEvent / EntityDeselectedEvent
// to know which item to inspect. Stores non-owning pointers to the selected
// entity/light/camera. Emits events for material changes, camera settings
// changes, and command execution.
// ================================================================================

class InspectorPanel : public EditorPanel {
public:
    InspectorPanel() = default;

    void Draw() override;
    void OnEvent(Engine::Events::Event& event) override;

private:
    void DrawObjectInspector();
    void DrawLightInspector();
    void DrawCameraInspector();

    // Selection state — received via EntitySelectedEvent
    EntitySelectedEvent::ItemType m_SelectedItemType = EntitySelectedEvent::ItemType::None;
    int m_SelectedObjectIndex = -1;
    int m_SelectedLightIndex = -1;
    int m_SelectedCameraIndex = -1;

    // Non-owning pointers to the inspected objects
    Renderer::Entity* m_SelectedEntity = nullptr;
    Renderer::SpotLight* m_SelectedLight = nullptr;
    std::shared_ptr<Renderer::Camera> m_SelectedCamera;

    // Euler degrees for the selected object (display-only, not persisted)
    DirectX::XMFLOAT3 m_SelectedEulerDegrees = { 0.0f, 0.0f, 0.0f };

    // Camera settings cache — received via CameraSettingsChangedEvent
    float m_CameraFov = DirectX::XM_PIDIV4;
    float m_CameraNearPlane = 0.1f;
    float m_CameraFarPlane = 100.0f;

    // Position Undo/Redo tracking
    DirectX::XMFLOAT3 m_PositionBeforeEdit = { 0.0f, 0.0f, 0.0f };

#if defined(_DEBUG)
    bool m_ShowDebugLightGizmos = true;
    bool m_ShowDebugWireframePlane = true;
#endif
};

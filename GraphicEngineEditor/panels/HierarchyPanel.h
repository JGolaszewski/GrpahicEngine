#pragma once

#include "EditorPanel.h"
#include "events/EditorEvents.h"
#include "Scene.h"
#include <DirectXMath.h>
#include <memory>
#include <vector>

namespace Renderer {
    class Entity;
    class SpotLight;
    class Camera;
}

// ================================================================================
// HierarchyPanel.h
// ================================================================================
// Decoupled from the Editor. Maintains its own copy of scene counts and
// selection state, received via events. Emits EntitySelectedEvent when the
// user clicks an item, and AddObjectRequestedEvent from the spawn modal.
// ================================================================================

class HierarchyPanel : public EditorPanel {
public:
    HierarchyPanel() = default;

    void Draw() override;
    void OnEvent(Engine::Events::Event& event) override;

private:
    void DrawAddObjectModal();

    // Internal state — synchronised via events
    std::vector<Engine::EntityId> m_ObjectIds;
    std::vector<Engine::LightId> m_LightIds;
    std::vector<Engine::CameraId> m_CameraIds;

    EntitySelectedEvent::ItemType m_SelectedItemType = EntitySelectedEvent::ItemType::None;
    int m_SelectedObjectIndex = -1;
    int m_SelectedLightIndex = -1;
    int m_SelectedCameraIndex = -1;

    // Spawn modal state
    enum class SpawnShapeType {
        Cube,
        Sphere,
        Plane,
        Cone
    };

    SpawnShapeType m_SpawnShape = SpawnShapeType::Cube;
    DirectX::XMFLOAT3 m_SpawnPosition = { 0.0f, 0.0f, 8.0f };
    DirectX::XMFLOAT3 m_SpawnScale = { 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4 m_SpawnColor = { 0.95f, 0.95f, 0.95f, 1.0f };
    DirectX::XMFLOAT3 m_SpawnEmissive = { 0.0f, 0.0f, 0.0f };
    float m_SpawnMetallic = 0.0f;
    float m_SpawnRoughness = 0.8f;
};

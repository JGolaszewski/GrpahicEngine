#pragma once

#include "assets/include/AssetManager.h"
#include "panels/include/EditorPanel.h"
#include "scene/include/Scene.h"
#include "events/include/EditorEvents.h"
#include <DirectXMath.h>
#include <memory>
#include <vector>

namespace Engine::Renderer
{
class Entity;
class SpotLight;
class Camera;
} // namespace Engine::Renderer

// ================================================================================
// HierarchyPanel.h
// ================================================================================
// Decoupled from the Editor. Maintains its own copy of scene counts and
// selection state, received via events. Emits EntitySelectedEvent when the
// user clicks an item, and AddObjectRequestedEvent from the spawn modal.
// ================================================================================

class HierarchyPanel : public EditorPanel
{
  public:
    HierarchyPanel() = default;

    void Draw() override;
    void OnEvent(Engine::Events::Event &event) override;

  private:
    void DrawAddObjectModal();

    // Internal state — synchronised via events
    std::vector<Engine::EntityId> m_ObjectIds;
    std::vector<Engine::LightId> m_LightIds;
    std::vector<Engine::CameraId> m_CameraIds;
    std::shared_ptr<Engine::Scene> m_Scene;

    EntitySelectedEvent::ItemType m_SelectedItemType = EntitySelectedEvent::ItemType::None;
    int m_SelectedObjectIndex = -1;
    Engine::EntityId m_SelectedObjectId = Engine::InvalidEntityId;
    int m_SelectedLightIndex = -1;
    int m_SelectedCameraIndex = -1;

    void DrawEntityTree(const Engine::Object &parent);
    void DrawEntityNode(const std::shared_ptr<Engine::Object> &object);
    void DrawRootDropTarget();

    // Spawn modal state
    enum class SpawnShapeType
    {
        Cube,
        Sphere,
        Plane,
        Cone
    };

    SpawnShapeType m_SpawnShape = SpawnShapeType::Cube;
    DirectX::XMFLOAT3 m_SpawnPosition = {0.0f, 0.0f, 8.0f};
    DirectX::XMFLOAT3 m_SpawnScale = {1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT4 m_SpawnColor = {0.95f, 0.95f, 0.95f, 1.0f};
    DirectX::XMFLOAT3 m_SpawnEmissive = {0.0f, 0.0f, 0.0f};
    float m_SpawnMetallic = 0.0f;
    float m_SpawnRoughness = 0.8f;
    std::vector<Engine::AssetManager::ModelAssetInfo> m_Models;
    int m_SelectedModelIndex = 0;
};

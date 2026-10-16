#pragma once

#include "assets/include/AssetManager.h"
#include "events/include/AssetRegistryChangedEvent.h"
#include "events/include/EcsComponentMutationRequestedEvent.h"
#include "events/include/EcsEntitySelectedEvent.h"
#include "events/include/EntityDeselectedEvent.h"
#include "events/include/SceneChangedEvent.h"
#include "panels/include/EditorPanel.h"
#include "scene/include/Scene.h"

#include <array>
#include <memory>
#include <vector>

class InspectorPanel final : public EditorPanel {
public:
  void Draw() override;
  void OnEvent(Engine::Events::Event& event) override;

private:
  void DrawEntityInspector();
  std::shared_ptr<Engine::Scene> m_Scene;
  Engine::EntityHandle m_SelectedEntity = Engine::InvalidEntityHandle;
  std::array<char, 64> m_AddComponentFilter{};
  Engine::EntityHandle m_CustomResolutionEntity = Engine::InvalidEntityHandle;
  Engine::EntityHandle m_MeshSourceSelectionEntity = Engine::InvalidEntityHandle;
  int m_MeshSourceSelectionIndex = 0;
  std::vector<Engine::AssetManager::ModelAssetInfo> m_Models;
};

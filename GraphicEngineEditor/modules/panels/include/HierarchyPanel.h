#pragma once

#include "events/include/EcsEntityCreateRequestedEvent.h"
#include "events/include/EcsEntityDeleteRequestedEvent.h"
#include "events/include/EcsEntityReparentRequestedEvent.h"
#include "events/include/EcsEntitySelectedEvent.h"
#include "events/include/EntityDeselectedEvent.h"
#include "events/include/SceneChangedEvent.h"
#include "panels/include/EditorPanel.h"
#include "scene/include/Scene.h"

#include <array>
#include <memory>
#include <vector>

class HierarchyPanel final : public EditorPanel {
public:
  void Draw() override;
  void OnEvent(Engine::Events::Event& event) override;

private:
  void DrawEntityNode(const Engine::Scene::EntitySnapshot& entity,
                      const std::vector<Engine::Scene::EntitySnapshot>& entities);
  [[nodiscard]] auto SubtreeMatches(const Engine::Scene::EntitySnapshot& entity,
                                    const std::vector<Engine::Scene::EntitySnapshot>& entities) const -> bool;
  [[nodiscard]] auto MatchesFilter(const Engine::Scene::EntitySnapshot& entity) const -> bool;
  void DrawCreateMenu(Engine::EntityHandle parent);

  std::shared_ptr<Engine::Scene> m_Scene;
  Engine::EntityHandle m_SelectedEntity = Engine::InvalidEntityHandle;
  std::array<char, 128> m_NewEntityName{'E', 'n', 't', 'i', 't', 'y', '\0'};
  std::array<char, 128> m_Filter{};
};

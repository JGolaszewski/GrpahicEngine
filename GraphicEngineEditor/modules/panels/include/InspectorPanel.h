#pragma once

#include "events/include/DebugRenderToggledEvent.h"
#include "events/include/EcsComponentMutationRequestedEvent.h"
#include "events/include/EcsEntitySelectedEvent.h"
#include "events/include/EntityDeselectedEvent.h"
#include "events/include/SceneChangedEvent.h"
#include "panels/include/EditorPanel.h"
#include "scene/include/Scene.h"

#include <array>
#include <memory>

class InspectorPanel final : public EditorPanel {
public:
  void Draw() override;
  void OnEvent(Engine::Events::Event& event) override;

private:
  void DrawEntityInspector();
  std::shared_ptr<Engine::Scene> m_Scene;
  Engine::EntityHandle m_SelectedEntity = Engine::InvalidEntityHandle;
  bool m_ShowDebugLightGizmos = true;
  bool m_ShowDebugWireframePlane = true;
  std::array<char, 64> m_AddComponentFilter{};
};

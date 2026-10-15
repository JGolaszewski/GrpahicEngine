#pragma once

#include "runtime/include/events/Event.h"
#include "scene/include/Scene.h"

#include <memory>

class SceneChangedEvent final : public Engine::Events::Event {
public:
  explicit SceneChangedEvent(std::shared_ptr<Engine::Scene> scene) : m_Scene(std::move(scene)) {}

  static auto GetStaticType() -> Engine::Events::EventType { return Engine::Events::EventType::SceneChanged; }
  auto GetType() const -> Engine::Events::EventType override { return GetStaticType(); }
  [[nodiscard]] auto GetScene() const noexcept -> const std::shared_ptr<Engine::Scene>& { return m_Scene; }

private:
  std::shared_ptr<Engine::Scene> m_Scene;
};

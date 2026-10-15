#pragma once

#include "runtime/include/events/Event.h"
#include "scene/include/Scene.h"

#include <string>
#include <utility>

class EcsEntityCreateRequestedEvent final : public Engine::Events::Event {
public:
  explicit EcsEntityCreateRequestedEvent(std::string name, Engine::EntityHandle parent = Engine::InvalidEntityHandle,
                                         Engine::Scene::EntityPreset preset = Engine::Scene::EntityPreset::Empty)
      : m_Name(std::move(name)), m_Parent(parent), m_Preset(preset) {}

  static auto GetStaticType() -> Engine::Events::EventType {
    return Engine::Events::EventType::EcsEntityCreateRequested;
  }
  auto GetType() const -> Engine::Events::EventType override { return GetStaticType(); }
  auto GetName() const noexcept -> const std::string& { return m_Name; }
  auto GetParent() const noexcept -> Engine::EntityHandle { return m_Parent; }
  auto GetPreset() const noexcept -> Engine::Scene::EntityPreset { return m_Preset; }

private:
  std::string m_Name;
  Engine::EntityHandle m_Parent;
  Engine::Scene::EntityPreset m_Preset;
};

#pragma once

#include "runtime/include/events/Event.h"
#include "scene/include/Scene.h"

#include <string>
#include <utility>

class MaterialAssetSelectionRequestedEvent final : public Engine::Events::Event {
public:
  MaterialAssetSelectionRequestedEvent(Engine::EntityHandle entity, std::string guid)
      : m_Entity(entity), m_Guid(std::move(guid)) {}

  static Engine::Events::EventType GetStaticType() {
    return Engine::Events::EventType::MaterialAssetSelectionRequested;
  }
  Engine::Events::EventType GetType() const override { return GetStaticType(); }
  [[nodiscard]] Engine::EntityHandle GetEntity() const noexcept { return m_Entity; }
  [[nodiscard]] const std::string& GetGuid() const noexcept { return m_Guid; }

private:
  Engine::EntityHandle m_Entity;
  std::string m_Guid;
};

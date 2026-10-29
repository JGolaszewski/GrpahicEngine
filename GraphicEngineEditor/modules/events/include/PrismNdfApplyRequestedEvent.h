#pragma once

#include "runtime/include/events/Event.h"
#include "scene/include/Scene.h"
#include "scene/include/SceneDocument.h"

#include <string>
#include <utility>

class PrismNdfApplyRequestedEvent final : public Engine::Events::Event {
public:
  PrismNdfApplyRequestedEvent(Engine::EntityHandle entity, std::string materialGuid,
                              Engine::SceneGeneratedNdfDocument generated)
      : m_Entity(entity), m_MaterialGuid(std::move(materialGuid)), m_Generated(std::move(generated)) {}
  static auto GetStaticType() -> Engine::Events::EventType { return Engine::Events::EventType::PrismNdfApplyRequested; }
  auto GetType() const -> Engine::Events::EventType override { return GetStaticType(); }
  [[nodiscard]] auto GetEntity() const noexcept -> Engine::EntityHandle { return m_Entity; }
  [[nodiscard]] auto GetMaterialGuid() const noexcept -> const std::string& { return m_MaterialGuid; }
  [[nodiscard]] auto GetGenerated() const noexcept -> const Engine::SceneGeneratedNdfDocument& { return m_Generated; }

private:
  Engine::EntityHandle m_Entity;
  std::string m_MaterialGuid;
  Engine::SceneGeneratedNdfDocument m_Generated;
};

#pragma once

#include "runtime/include/events/Event.h"
#include "scene/include/ArchetypeWorld.h"
#include "scene/include/components/EcsComponents.h"

#include <variant>

class EcsComponentMutationRequestedEvent final : public Engine::Events::Event {
public:
  enum class Operation : unsigned char { Add, Remove, Set };
  enum class Component : unsigned char { Name, Transform, LocalVisibility, MeshRenderer, SpotLight, Camera };
  using Payload =
      std::variant<std::monostate, Engine::Ecs::Name, Engine::Ecs::LocalTransform, Engine::Ecs::LocalVisibility,
                   Engine::Ecs::MeshRenderer, Engine::Ecs::SpotLight, Engine::Ecs::Camera>;

  EcsComponentMutationRequestedEvent(Engine::EntityHandle entity, Operation operation, Component component,
                                     Payload payload = {})
      : m_Entity(entity), m_Operation(operation), m_Component(component), m_Payload(std::move(payload)) {}

  static auto GetStaticType() -> Engine::Events::EventType {
    return Engine::Events::EventType::EcsComponentMutationRequested;
  }
  auto GetType() const -> Engine::Events::EventType override { return GetStaticType(); }
  auto GetEntity() const noexcept -> Engine::EntityHandle { return m_Entity; }
  auto GetOperation() const noexcept -> Operation { return m_Operation; }
  auto GetComponent() const noexcept -> Component { return m_Component; }
  auto TakePayload() -> Payload { return std::move(m_Payload); }

private:
  Engine::EntityHandle m_Entity;
  Operation m_Operation;
  Component m_Component;
  Payload m_Payload;
};

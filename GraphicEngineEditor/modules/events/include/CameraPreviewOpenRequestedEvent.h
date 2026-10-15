#pragma once

#include "runtime/include/events/Event.h"
#include "scene/include/ArchetypeWorld.h"

/// Requests the full-resolution viewer for the clicked camera preview.
class CameraPreviewOpenRequestedEvent final : public Engine::Events::Event {
public:
  explicit CameraPreviewOpenRequestedEvent(Engine::EntityHandle entity) : m_Entity(entity) {}

  static auto GetStaticType() -> Engine::Events::EventType {
    return Engine::Events::EventType::CameraPreviewOpenRequested;
  }
  auto GetType() const -> Engine::Events::EventType override { return GetStaticType(); }
  [[nodiscard]] auto GetEntity() const noexcept -> Engine::EntityHandle { return m_Entity; }

private:
  Engine::EntityHandle m_Entity = Engine::InvalidEntityHandle;
};

#pragma once

#include "runtime/include/events/Event.h"
#include "scene/include/AmbientLightSettings.h"

class SceneAmbientLightChangedEvent final : public Engine::Events::Event {
public:
  explicit SceneAmbientLightChangedEvent(Engine::AmbientLightSettings settings) : Settings(std::move(settings)) {}

  static auto GetStaticType() -> Engine::Events::EventType {
    return Engine::Events::EventType::SceneAmbientLightChanged;
  }
  auto GetType() const -> Engine::Events::EventType override { return GetStaticType(); }

  Engine::AmbientLightSettings Settings;
};

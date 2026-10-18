#pragma once

#include "runtime/include/events/Event.h"
#include "scene/include/Scene.h"

#include <string>
#include <utility>

class MaterialAssetEditRequestedEvent final : public Engine::Events::Event {
public:
  MaterialAssetEditRequestedEvent(std::string guid, Engine::SceneMaterialDocument material)
      : m_Guid(std::move(guid)), m_Material(std::move(material)) {}

  static Engine::Events::EventType GetStaticType() { return Engine::Events::EventType::MaterialAssetEditRequested; }
  Engine::Events::EventType GetType() const override { return GetStaticType(); }
  [[nodiscard]] const std::string& GetGuid() const noexcept { return m_Guid; }
  [[nodiscard]] const Engine::SceneMaterialDocument& GetMaterial() const noexcept { return m_Material; }

private:
  std::string m_Guid;
  Engine::SceneMaterialDocument m_Material;
};

#pragma once

#include "runtime/include/events/Event.h"

#include <string>
#include <utility>

class AssetRenameRequestedEvent final : public Engine::Events::Event {
public:
  AssetRenameRequestedEvent(std::string guid, std::string newName)
      : m_Guid(std::move(guid)), m_NewName(std::move(newName)) {}
  static Engine::Events::EventType GetStaticType() { return Engine::Events::EventType::AssetRenameRequested; }
  Engine::Events::EventType GetType() const override { return GetStaticType(); }
  const std::string& GetGuid() const noexcept { return m_Guid; }
  const std::string& GetNewName() const noexcept { return m_NewName; }

private:
  std::string m_Guid;
  std::string m_NewName;
};

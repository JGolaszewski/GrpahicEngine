#pragma once

#include "events/Event.h"
#include <string>
#include <utility>

class AssetReimportRequestedEvent final : public Engine::Events::Event
{
  public:
    explicit AssetReimportRequestedEvent(std::string guid) : m_Guid(std::move(guid))
    {
    }
    static Engine::Events::EventType GetStaticType()
    {
        return Engine::Events::EventType::AssetReimportRequested;
    }
    Engine::Events::EventType GetType() const override
    {
        return GetStaticType();
    }
    const std::string &GetGuid() const noexcept
    {
        return m_Guid;
    }

  private:
    std::string m_Guid;
};

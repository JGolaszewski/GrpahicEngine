#pragma once

#include "runtime/include/events/Event.h"
#include "events/include/SceneCreationResult.h"
#include <DirectXMath.h>
#include <memory>
#include <string>
#include <utility>

class AddModelRequestedEvent final : public Engine::Events::Event
{
  public:
    AddModelRequestedEvent(std::string guid, DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 scale,
                           std::string name = {})
        : m_Guid(std::move(guid)), m_Position(position), m_Scale(scale), m_Name(std::move(name))
    {
    }
    static Engine::Events::EventType GetStaticType()
    {
        return Engine::Events::EventType::AddModelRequested;
    }
    Engine::Events::EventType GetType() const override
    {
        return GetStaticType();
    }
    const std::string &GetGuid() const noexcept
    {
        return m_Guid;
    }
    const DirectX::XMFLOAT3 &GetPosition() const noexcept
    {
        return m_Position;
    }
    const DirectX::XMFLOAT3 &GetScale() const noexcept
    {
        return m_Scale;
    }
    const std::string &GetName() const noexcept
    {
        return m_Name;
    }
    const std::shared_ptr<SceneCreationResult> &GetCreationResult() const noexcept
    {
        return m_CreationResult;
    }

  private:
    std::string m_Guid;
    DirectX::XMFLOAT3 m_Position;
    DirectX::XMFLOAT3 m_Scale;
    std::string m_Name;
    std::shared_ptr<SceneCreationResult> m_CreationResult = std::make_shared<SceneCreationResult>();
};

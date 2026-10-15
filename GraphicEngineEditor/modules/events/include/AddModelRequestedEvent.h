#pragma once

#include "runtime/include/events/Event.h"
#include <DirectXMath.h>
#include <string>
#include <utility>

class AddModelRequestedEvent final : public Engine::Events::Event {
public:
  AddModelRequestedEvent(std::string guid, DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 scale)
      : m_Guid(std::move(guid)), m_Position(position), m_Scale(scale) {}
  static Engine::Events::EventType GetStaticType() { return Engine::Events::EventType::AddModelRequested; }
  Engine::Events::EventType GetType() const override { return GetStaticType(); }
  const std::string& GetGuid() const noexcept { return m_Guid; }
  const DirectX::XMFLOAT3& GetPosition() const noexcept { return m_Position; }
  const DirectX::XMFLOAT3& GetScale() const noexcept { return m_Scale; }

private:
  std::string m_Guid;
  DirectX::XMFLOAT3 m_Position;
  DirectX::XMFLOAT3 m_Scale;
};

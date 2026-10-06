#pragma once

#include "events/Event.h"
#include <DirectXMath.h>

// ================================================================================
// AddObjectRequestedEvent.h
// ================================================================================
// Emitted by the HierarchyPanel when the user clicks "Add" in the spawn modal.
// The Editor receives this, creates the entity via EntityBuilder, and then
// broadcasts SceneChangedEvent and EntitySelectedEvent.
// ================================================================================

class AddObjectRequestedEvent : public Engine::Events::Event
{
  public:
    enum class Shape
    {
        Cube,
        Sphere,
        Plane,
        Cone
    };

    AddObjectRequestedEvent(Shape shape, const DirectX::XMFLOAT3 &position, const DirectX::XMFLOAT3 &scale,
                            const DirectX::XMFLOAT4 &color, const DirectX::XMFLOAT3 &emissive, float metallic,
                            float roughness)
        : m_Shape(shape), m_Position(position), m_Scale(scale), m_Color(color), m_Emissive(emissive),
          m_Metallic(metallic), m_Roughness(roughness)
    {
    }

    static Engine::Events::EventType GetStaticType()
    {
        return Engine::Events::EventType::AddObjectRequested;
    }

    Engine::Events::EventType GetType() const override
    {
        return GetStaticType();
    }

    [[nodiscard]] Shape GetShape() const noexcept
    {
        return m_Shape;
    }
    [[nodiscard]] const DirectX::XMFLOAT3 &GetPosition() const noexcept
    {
        return m_Position;
    }
    [[nodiscard]] const DirectX::XMFLOAT3 &GetScale() const noexcept
    {
        return m_Scale;
    }
    [[nodiscard]] const DirectX::XMFLOAT4 &GetColor() const noexcept
    {
        return m_Color;
    }
    [[nodiscard]] const DirectX::XMFLOAT3 &GetEmissive() const noexcept
    {
        return m_Emissive;
    }
    [[nodiscard]] float GetMetallic() const noexcept
    {
        return m_Metallic;
    }
    [[nodiscard]] float GetRoughness() const noexcept
    {
        return m_Roughness;
    }

  private:
    Shape m_Shape;
    DirectX::XMFLOAT3 m_Position;
    DirectX::XMFLOAT3 m_Scale;
    DirectX::XMFLOAT4 m_Color;
    DirectX::XMFLOAT3 m_Emissive;
    float m_Metallic;
    float m_Roughness;
};

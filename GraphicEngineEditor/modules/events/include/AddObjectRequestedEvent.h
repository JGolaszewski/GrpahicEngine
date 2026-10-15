#pragma once

#include "runtime/include/events/Event.h"
#include "events/include/SceneCreationResult.h"
#include <DirectXMath.h>
#include <memory>
#include <string>
#include <utility>

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
                            float roughness, std::string name = {})
        : m_Shape(shape), m_Position(position), m_Scale(scale), m_Color(color), m_Emissive(emissive),
          m_Metallic(metallic), m_Roughness(roughness), m_Name(std::move(name))
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
    [[nodiscard]] const std::string &GetName() const noexcept
    {
        return m_Name;
    }
    [[nodiscard]] const std::shared_ptr<SceneCreationResult> &GetCreationResult() const noexcept
    {
        return m_CreationResult;
    }

  private:
    Shape m_Shape;
    DirectX::XMFLOAT3 m_Position;
    DirectX::XMFLOAT3 m_Scale;
    DirectX::XMFLOAT4 m_Color;
    DirectX::XMFLOAT3 m_Emissive;
    float m_Metallic;
    float m_Roughness;
    std::string m_Name;
    std::shared_ptr<SceneCreationResult> m_CreationResult = std::make_shared<SceneCreationResult>();
};

#pragma once

#include "runtime/include/CoreExport.h"

#include <DirectXMath.h>
#include <cstdint>

#include <scene/components/Transform.h>

namespace Engine::Renderer
{
class ENGINE_API Camera
{
  public:
    ::Engine::Scene::Components::Transform m_Transform;

    // Projection values use radians for FOV and positive distances for planes.
    Camera(float fovRadians, float znear, float zfar);

    bool SetProjection(float fovRadians, float znear, float zfar);
    float GetFovRadians() const noexcept
    {
        return m_FovRadians;
    }
    float GetNearPlane() const noexcept
    {
        return m_Near;
    }
    float GetFarPlane() const noexcept
    {
        return m_Far;
    }

    void SetPosition(::Engine::Scene::Components::Transform transform);

    DirectX::XMMATRIX GetView() const;
    DirectX::XMMATRIX GetProjection(float aspect) const;
    DirectX::XMMATRIX GetViewProjection(float aspect) const;

  private:
    float m_FovRadians = DirectX::XM_PIDIV4;
    float m_Near = 0.0f;
    float m_Far = 0.0f;
    mutable bool m_ProjectionDirty = true;
    mutable float m_CachedAspect = 0.0f;
    mutable DirectX::XMMATRIX m_CachedProjection = DirectX::XMMatrixIdentity();
};
} // namespace Engine::Renderer

#include "rendering/include/renderer/Camera.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Engine::Renderer
{
Camera::Camera(float fovRadians, float znear, float zfar)
{
    if (!SetProjection(fovRadians, znear, zfar))
    {
        SetProjection(DirectX::XM_PIDIV4, 0.1f, 100.0f);
    }
}

bool Camera::SetProjection(float fovRadians, float znear, float zfar)
{
    if (!std::isfinite(fovRadians) || !std::isfinite(znear) || !std::isfinite(zfar))
    {
        return false;
    }
    if (fovRadians <= 0.01f || fovRadians >= DirectX::XM_PI - 0.01f)
    {
        return false;
    }
    if (znear <= 0.0f || zfar <= znear)
    {
        return false;
    }

    m_FovRadians = fovRadians;
    m_Near = znear;
    m_Far = zfar;
    m_ProjectionDirty = true;
    return true;
}

void Camera::SetPosition(::Engine::Components::Transform transform)
{
    this->transform.SetPosition(transform.Position.x, transform.Position.y, transform.Position.z);
    this->transform.Rotation = transform.Rotation;
    this->transform.SetScale(transform.Scale.x, transform.Scale.y, transform.Scale.z);
}

DirectX::XMMATRIX Camera::GetView() const
{
    return DirectX::XMMatrixInverse(nullptr, transform.GetWorldMat());
}

DirectX::XMMATRIX Camera::GetProjection(float aspect) const
{
    const float safeAspect = std::max(aspect, 0.0001f);
    if (m_ProjectionDirty || safeAspect != m_CachedAspect)
    {
        m_CachedProjection = DirectX::XMMatrixPerspectiveFovLH(
            safeAspect > 0.0f ? m_FovRadians : DirectX::XM_PIDIV4, safeAspect, m_Near, m_Far);
        m_CachedAspect = safeAspect;
        m_ProjectionDirty = false;
    }
    return m_CachedProjection;
}

DirectX::XMMATRIX Camera::GetViewProjection(float aspect) const
{
    return GetView() * GetProjection(aspect);
}

} // namespace Engine::Renderer

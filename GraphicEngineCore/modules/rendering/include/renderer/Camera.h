#pragma once

#include "runtime/include/CoreExport.h"

#include <DirectXMath.h>
#include <cstdint>

#include "scene/include/components/Transform.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Renderer
{
/// Perspective camera with cached projection and hierarchical transform support.
class ENGINE_API Camera
{
  public:
    ::Engine::Components::Transform m_Transform; ///< Camera transform in the scene hierarchy.

    /// Creates a perspective camera, using a default projection if the arguments are invalid.
    /// @param fovRadians Vertical field of view in radians.
    /// @param znear Positive near clipping distance.
    /// @param zfar Far clipping distance greater than znear.
    Camera(float fovRadians, float znear, float zfar);

    /// Sets projection parameters after validating their finite ranges.
    /// @param fovRadians Vertical field of view in radians.
    /// @param znear Positive near clipping distance.
    /// @param zfar Far clipping distance greater than znear.
    /// @return True when the projection values are valid and applied.
    bool SetProjection(float fovRadians, float znear, float zfar);

    /// Returns the current vertical field of view.
    /// @return Field of view in radians.
    float GetFovRadians() const noexcept
    {
        return m_FovRadians;
    }
    /// Returns the near clipping distance.
    /// @return Positive near-plane distance.
    float GetNearPlane() const noexcept
    {
        return m_Near;
    }
    /// Returns the far clipping distance.
    /// @return Far-plane distance greater than the near plane.
    float GetFarPlane() const noexcept
    {
        return m_Far;
    }

    /// Replaces the camera transform.
    /// @param transform New camera transform.
    void SetPosition(::Engine::Components::Transform transform);

    /// Returns the inverse world transform used for view-space conversion.
    /// @return View matrix.
    DirectX::XMMATRIX GetView() const;

    /// Returns a left-handed perspective projection for a viewport aspect ratio.
    /// @param aspect Viewport width divided by height.
    /// @return Projection matrix.
    DirectX::XMMATRIX GetProjection(float aspect) const;

    /// Returns the view matrix multiplied by the perspective projection.
    /// @param aspect Viewport width divided by height.
    /// @return View-projection matrix.
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

#ifdef _MSC_VER
#pragma warning(pop)
#endif

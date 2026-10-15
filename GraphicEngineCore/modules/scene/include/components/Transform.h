#pragma once

#include "runtime/include/CoreExport.h"
#include "scene/include/Component.h"
#include <DirectXMath.h>
#include <cstdint>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Components
{
/// Stores local translation, quaternion rotation, and scale with cached hierarchical matrices.
class ENGINE_API Transform : public Component
{
  public:
    /// Sets local translation and invalidates cached matrices.
    /// @param positionX Local X position.
    /// @param positionY Local Y position.
    /// @param positionZ Local Z position.
    void SetPosition(float positionX, float positionY, float positionZ);

    /// Sets local rotation from pitch, yaw, and roll angles in radians.
    /// @param pitch Rotation about the local X axis.
    /// @param yaw Rotation about the local Y axis.
    /// @param roll Rotation about the local Z axis.
    void SetRotation(float pitch, float yaw, float roll);

    /// Sets local scale and invalidates cached matrices.
    /// @param scaleX Local X scale.
    /// @param scaleY Local Y scale.
    /// @param scaleZ Local Z scale.
    void SetScale(float scaleX, float scaleY, float scaleZ);

    /// Adds a local-space translation.
    /// @param offsetX X translation.
    /// @param offsetY Y translation.
    /// @param offsetZ Z translation.
    void Translate(float offsetX, float offsetY, float offsetZ);

    /// Multiplies local scale by per-axis factors.
    /// @param factorX X scale multiplier.
    /// @param factorY Y scale multiplier.
    /// @param factorZ Z scale multiplier.
    void ScaleBy(float factorX, float factorY, float factorZ);

    /// Applies an incremental pitch, yaw, and roll rotation in radians.
    /// @param pitch Rotation about the local X axis.
    /// @param yaw Rotation about the local Y axis.
    /// @param roll Rotation about the local Z axis.
    void Rotate(float pitch, float yaw, float roll);

    /// Rotates the transform to face a world-space target.
    /// @param targetX Target X position.
    /// @param targetY Target Y position.
    /// @param targetZ Target Z position.
    void LookAt(float targetX, float targetY, float targetZ);

    /// Sets the parent transform while rejecting cycles.
    /// @param parent Parent transform, or null to detach.
    /// @return True when the parent changed; false when rejected or unchanged.
    bool SetParent(Transform *parent);

    /// Returns the current parent transform.
    /// @return Borrowed parent pointer, or null when unparented.
    Transform *GetParent() const noexcept
    {
        return m_Parent;
    }

    /// Returns the cached local transform matrix.
    /// @return Local scale-rotation-translation matrix.
    DirectX::XMMATRIX GetLocalMat() const;

    /// Returns the transform composed with all ancestors.
    /// @return World matrix.
    DirectX::XMMATRIX GetWorldMat() const;

    /// Returns the inverse-transpose matrix used for normal transformation.
    /// @return Finite world normal matrix.
    DirectX::XMMATRIX GetNormalMat() const;

    /// Returns the revision of the cached world transform.
    /// @return Monotonically increasing world revision.
    uint64_t GetWorldRevision() const;

    DirectX::XMFLOAT3 Position = {0.0f, 0.0f, 0.0f};       ///< Local translation in engine units.
    DirectX::XMFLOAT4 Rotation = {0.0f, 0.0f, 0.0f, 1.0f}; ///< Local quaternion in XYZW order.
    DirectX::XMFLOAT3 Scale = {1.0f, 1.0f, 1.0f};          ///< Local scale on each axis.

  private:
    void MarkDirty() noexcept
    {
        m_LocalMatrixDirty = true;
        m_WorldMatrixDirty = true;
    }

    Transform *m_Parent = nullptr;
    mutable bool m_LocalMatrixDirty = true;
    mutable DirectX::XMFLOAT3 m_CachedPosition = {};
    mutable DirectX::XMFLOAT4 m_CachedRotation = {};
    mutable DirectX::XMFLOAT3 m_CachedScale = {};
    mutable DirectX::XMMATRIX m_CachedLocalMatrix = DirectX::XMMatrixIdentity();
    mutable DirectX::XMMATRIX m_CachedWorldMatrix = DirectX::XMMatrixIdentity();
    mutable uint64_t m_WorldRevision = 0;
    mutable uint64_t m_CachedParentWorldRevision = 0;
    mutable bool m_WorldMatrixDirty = true;
};
} // namespace Engine::Components

#ifdef _MSC_VER
#pragma warning(pop)
#endif

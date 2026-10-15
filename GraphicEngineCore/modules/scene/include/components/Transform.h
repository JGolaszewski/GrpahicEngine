#pragma once

#include "runtime/include/CoreExport.h"
#include <DirectXMath.h>
#include <cstdint>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Components
{
/// Local position, quaternion rotation, and scale with cached hierarchical matrices.
class ENGINE_API Transform
{
  public:
    /// Sets local translation and invalidates the cached local matrix.
    /// @param x Local position on the X axis.
    /// @param y Local position on the Y axis.
    /// @param z Local position on the Z axis.
    void SetPosition(float x, float y, float z);

    /// Sets local rotation from pitch, yaw, and roll angles in radians.
    /// @param pitch Rotation about the local X axis.
    /// @param yaw Rotation about the local Y axis.
    /// @param roll Rotation about the local Z axis.
    void SetRotation(float pitch, float yaw, float roll);

    /// Sets local scale and invalidates the cached local matrix.
    /// @param x Scale on the X axis.
    /// @param y Scale on the Y axis.
    /// @param z Scale on the Z axis.
    void SetScale(float x, float y, float z);

    /// Adds a translation to the local position.
    /// @param x Translation on the X axis.
    /// @param y Translation on the Y axis.
    /// @param z Translation on the Z axis.
    void Translate(float x, float y, float z);

    /// Multiplies the current scale by per-axis factors.
    /// @param x Scale multiplier on the X axis.
    /// @param y Scale multiplier on the Y axis.
    /// @param z Scale multiplier on the Z axis.
    void ScaleBy(float x, float y, float z);

    /// Applies an incremental pitch, yaw, and roll rotation in radians.
    /// @param pitch Rotation about the local X axis.
    /// @param yaw Rotation about the local Y axis.
    /// @param roll Rotation about the local Z axis.
    void Rotate(float pitch, float yaw, float roll);

    /// Rotates this transform to face a world-space target from its current position.
    /// @param targetX Target X coordinate.
    /// @param targetY Target Y coordinate.
    /// @param targetZ Target Z coordinate.
    void LookAt(float targetX, float targetY, float targetZ);

    /// Sets the parent used when composing the world matrix.
    /// Cycles are rejected and leave the current parent unchanged.
    /// @param parent Parent transform, or null to detach.
    void SetParent(Transform *parent);

    /// Returns the current transform parent.
    /// @return Borrowed parent pointer, or null when unparented.
    Transform *GetParent() const noexcept
    {
        return m_Parent;
    }

    /// Returns the cached local scale-rotation-translation matrix.
    /// @return Local matrix in DirectX row-vector convention.
    DirectX::XMMATRIX GetLocalMat() const;

    /// Returns the local matrix composed with every ancestor transform.
    /// @return World matrix in DirectX row-vector convention.
    DirectX::XMMATRIX GetWorldMat() const;

    /// Returns the inverse-transpose matrix used to transform surface normals.
    /// @return Finite normal matrix, including when the world matrix is singular.
    DirectX::XMMATRIX GetNormalMat() const;

    DirectX::XMFLOAT3 Position = {0.0f, 0.0f, 0.0f};       ///< Local translation in engine units.
    DirectX::XMFLOAT4 Rotation = {0.0f, 0.0f, 0.0f, 1.0f}; ///< Local rotation quaternion in XYZW order.
    DirectX::XMFLOAT3 Scale = {1.0f, 1.0f, 1.0f};          ///< Local scale on each axis.

  private:
    void MarkDirty() noexcept
    {
        m_LocalMatrixDirty = true;
    }

    Transform *m_Parent = nullptr;
    mutable bool m_LocalMatrixDirty = true;
    mutable DirectX::XMFLOAT3 m_CachedPosition = {};
    mutable DirectX::XMFLOAT4 m_CachedRotation = {};
    mutable DirectX::XMFLOAT3 m_CachedScale = {};
    mutable DirectX::XMMATRIX m_CachedLocalMatrix = DirectX::XMMatrixIdentity();
};
} // namespace Engine::Components

#ifdef _MSC_VER
#pragma warning(pop)
#endif

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
class ENGINE_API Transform : public Component
{
  public:
    void SetPosition(float positionX, float positionY, float positionZ);

    void SetRotation(float pitch, float yaw, float roll);

    void SetScale(float scaleX, float scaleY, float scaleZ);

    void Translate(float offsetX, float offsetY, float offsetZ);

    void ScaleBy(float factorX, float factorY, float factorZ);

    void Rotate(float pitch, float yaw, float roll);

    void LookAt(float targetX, float targetY, float targetZ);

    bool SetParent(Transform *parent);
    Transform *GetParent() const noexcept
    {
        return m_Parent;
    }

    DirectX::XMMATRIX GetLocalMat() const;
    DirectX::XMMATRIX GetWorldMat() const;
    DirectX::XMMATRIX GetNormalMat() const;
    uint64_t GetWorldRevision() const;

    DirectX::XMFLOAT3 Position = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT3 Scale = {1.0f, 1.0f, 1.0f};

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

#pragma once

#include "CoreExport.h"
#include <DirectXMath.h>
#include <cstdint>

namespace Components
{
class ENGINE_API Transform
{
  public:
    void SetPosition(float x, float y, float z);

    void SetRotation(float pitch, float yaw, float roll);

    void SetScale(float x, float y, float z);

    void Translate(float x, float y, float z);

    void ScaleBy(float x, float y, float z);

    void Rotate(float pitch, float yaw, float roll);

    void LookAt(float targetX, float targetY, float targetZ);

    void SetParent(Transform *parent);
    Transform *GetParent() const noexcept
    {
        return m_Parent;
    }

    DirectX::XMMATRIX GetLocalMat() const;
    DirectX::XMMATRIX GetWorldMat() const;
    DirectX::XMMATRIX GetNormalMat() const;

    DirectX::XMFLOAT3 Position = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT3 Scale = {1.0f, 1.0f, 1.0f};

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
} // namespace Components

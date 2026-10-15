#include "scene/include/components/Transform.h"

#include <cmath>

void ::Engine::Components::Transform::SetPosition(float positionX, float positionY, float positionZ)
{
    Position = {positionX, positionY, positionZ};
    MarkDirty();
}

void ::Engine::Components::Transform::SetRotation(float pitch, float yaw, float roll)
{
    DirectX::XMVECTOR quat = DirectX::XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);
    quat = DirectX::XMQuaternionNormalize(quat);
    DirectX::XMStoreFloat4(&Rotation, quat);
    MarkDirty();
}

void ::Engine::Components::Transform::SetScale(float scaleX, float scaleY, float scaleZ)
{
    Scale = {scaleX, scaleY, scaleZ};
    MarkDirty();
}

void ::Engine::Components::Transform::Translate(float offsetX, float offsetY, float offsetZ)
{
    Position.x += offsetX;
    Position.y += offsetY;
    Position.z += offsetZ;
    MarkDirty();
}

void ::Engine::Components::Transform::ScaleBy(float factorX, float factorY, float factorZ)
{
    Scale.x *= factorX;
    Scale.y *= factorY;
    Scale.z *= factorZ;
    MarkDirty();
}

void ::Engine::Components::Transform::Rotate(float pitch, float yaw, float roll)
{
    using namespace DirectX;

    XMVECTOR currentRot = XMLoadFloat4(&Rotation);
    XMVECTOR deltaRot = XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);
    XMVECTOR newRot = XMQuaternionMultiply(currentRot, deltaRot);
    newRot = XMQuaternionNormalize(newRot);

    XMStoreFloat4(&Rotation, newRot);
    MarkDirty();
}

void ::Engine::Components::Transform::LookAt(float targetX, float targetY, float targetZ)
{
    using namespace DirectX;

    XMVECTOR pos = XMLoadFloat3(&Position);
    XMVECTOR target = XMVectorSet(targetX, targetY, targetZ, 0.0F);
    XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(target, pos));

    if (XMVector3Equal(forward, XMVectorZero()))
    {
        return;
    }

    XMVECTOR worldUp = XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F);
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));
    XMVECTOR correctedUp = XMVector3Cross(forward, right);

    XMMATRIX rotMat = XMMatrixIdentity();
    rotMat.r[0] = right;
    rotMat.r[1] = correctedUp;
    rotMat.r[2] = forward;

    XMVECTOR quat = XMQuaternionRotationMatrix(rotMat);
    XMStoreFloat4(&Rotation, quat);
    MarkDirty();
}

auto Engine::Components::Transform::SetParent(Transform *parent) -> bool
{
    if (parent == this)
    {
        return false;
    }

    for (Transform *ancestor = parent; ancestor != nullptr; ancestor = ancestor->m_Parent)
    {
        if (ancestor == this)
        {
            return false;
        }
    }

    if (parent == m_Parent)
    {
        return true;
    }
    m_Parent = parent;
    MarkDirty();
    return true;
}

auto Engine::Components::Transform::GetLocalMat() const -> DirectX::XMMATRIX
{
    using namespace DirectX;
    const bool inputsChanged = Position.x != m_CachedPosition.x || Position.y != m_CachedPosition.y ||
                               Position.z != m_CachedPosition.z || Rotation.x != m_CachedRotation.x ||
                               Rotation.y != m_CachedRotation.y || Rotation.z != m_CachedRotation.z ||
                               Rotation.w != m_CachedRotation.w || Scale.x != m_CachedScale.x ||
                               Scale.y != m_CachedScale.y || Scale.z != m_CachedScale.z;

    if (m_LocalMatrixDirty || inputsChanged)
    {
        XMVECTOR pos = XMLoadFloat3(&Position);
        XMVECTOR rot = XMLoadFloat4(&Rotation);
        XMVECTOR scale = XMLoadFloat3(&Scale);

        m_CachedLocalMatrix = XMMatrixAffineTransformation(scale, XMVectorZero(), rot, pos);
        m_CachedPosition = Position;
        m_CachedRotation = Rotation;
        m_CachedScale = Scale;
        m_LocalMatrixDirty = false;
        m_WorldMatrixDirty = true;
    }

    return m_CachedLocalMatrix;
}

auto Engine::Components::Transform::GetWorldMat() const -> DirectX::XMMATRIX
{
    const DirectX::XMMATRIX local = GetLocalMat();
    const DirectX::XMMATRIX parentWorld =
        m_Parent != nullptr ? m_Parent->GetWorldMat() : DirectX::XMMatrixIdentity();
    const uint64_t parentRevision = m_Parent != nullptr ? m_Parent->m_WorldRevision : 0;
    if (m_WorldMatrixDirty || parentRevision != m_CachedParentWorldRevision)
    {
        m_CachedWorldMatrix = m_Parent != nullptr ? local * parentWorld : local;
        m_CachedParentWorldRevision = parentRevision;
        m_WorldMatrixDirty = false;
        ++m_WorldRevision;
    }
    return m_CachedWorldMatrix;
}

auto Engine::Components::Transform::GetWorldRevision() const -> uint64_t
{
    (void)GetWorldMat();
    return m_WorldRevision;
}

auto Engine::Components::Transform::GetNormalMat() const -> DirectX::XMMATRIX
{
    using namespace DirectX;
    const XMMATRIX world = GetWorldMat();
    XMVECTOR determinant;
    const XMMATRIX inverse = XMMatrixInverse(&determinant, world);
    const float determinantValue = XMVectorGetX(determinant);
    if (!std::isfinite(determinantValue) || std::abs(determinantValue) < 1.0e-8F)
    {
        return XMMatrixIdentity();
    }
    return XMMatrixTranspose(inverse);
}

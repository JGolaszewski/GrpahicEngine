#include "components/Transform.h"

#include <cmath>

void Components::Transform::SetPosition(float x, float y, float z) {
	Position = { x, y, z };
	MarkDirty();
}

void Components::Transform::SetRotation(float pitch, float yaw, float roll) {
	DirectX::XMVECTOR quat = DirectX::XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);
	quat = DirectX::XMQuaternionNormalize(quat);
	DirectX::XMStoreFloat4(&Rotation, quat);
	MarkDirty();
}

void Components::Transform::SetScale(float x, float y, float z) {
	Scale = { x, y, z };
	MarkDirty();
}

void Components::Transform::Translate(float x, float y, float z) {
	Position.x += x;
	Position.y += y;
	Position.z += z;
	MarkDirty();
}

void Components::Transform::ScaleBy(float x, float y, float z) {
	Scale.x *= x;
	Scale.y *= y;
	Scale.z *= z;
	MarkDirty();
}

void Components::Transform::Rotate(float pitch, float yaw, float roll) {
	using namespace DirectX;

	XMVECTOR currentRot = XMLoadFloat4(&Rotation);
	XMVECTOR deltaRot = XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);
	XMVECTOR newRot = XMQuaternionMultiply(currentRot, deltaRot);
	newRot = XMQuaternionNormalize(newRot);

	XMStoreFloat4(&Rotation, newRot);
	MarkDirty();
}

void Components::Transform::LookAt(float targetX, float targetY, float targetZ) {
	using namespace DirectX;

	XMVECTOR pos = XMLoadFloat3(&Position);
	XMVECTOR target = XMVectorSet(targetX, targetY, targetZ, 0.0f);
	XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(target, pos));

	if (XMVector3Equal(forward, XMVectorZero())) return;

	XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));
	XMVECTOR up = XMVector3Cross(forward, right);

	XMMATRIX rotMat = XMMatrixIdentity();
	rotMat.r[0] = right;
	rotMat.r[1] = up;
	rotMat.r[2] = forward;

	XMVECTOR quat = XMQuaternionRotationMatrix(rotMat);
	XMStoreFloat4(&Rotation, quat);
	MarkDirty();
}

void Components::Transform::SetParent(Transform* parent) {
	if (parent == this) return;

	for (Transform* ancestor = parent; ancestor != nullptr; ancestor = ancestor->m_Parent) {
		if (ancestor == this) return;
	}

	m_Parent = parent;
	MarkDirty();
}

DirectX::XMMATRIX Components::Transform::GetLocalMat() const {
	using namespace DirectX;
	const bool inputsChanged = Position.x != m_CachedPosition.x ||
		Position.y != m_CachedPosition.y || Position.z != m_CachedPosition.z ||
		Rotation.x != m_CachedRotation.x || Rotation.y != m_CachedRotation.y ||
		Rotation.z != m_CachedRotation.z || Rotation.w != m_CachedRotation.w ||
		Scale.x != m_CachedScale.x || Scale.y != m_CachedScale.y || Scale.z != m_CachedScale.z;

	if (m_LocalMatrixDirty || inputsChanged) {
		XMVECTOR pos = XMLoadFloat3(&Position);
		XMVECTOR rot = XMLoadFloat4(&Rotation);
		XMVECTOR scale = XMLoadFloat3(&Scale);

		m_CachedLocalMatrix = XMMatrixAffineTransformation(scale, XMVectorZero(), rot, pos);
		m_CachedPosition = Position;
		m_CachedRotation = Rotation;
		m_CachedScale = Scale;
		m_LocalMatrixDirty = false;
	}

	return m_CachedLocalMatrix;
}

DirectX::XMMATRIX Components::Transform::GetWorldMat() const {
	const DirectX::XMMATRIX local = GetLocalMat();
	return m_Parent ? local * m_Parent->GetWorldMat() : local;
}

DirectX::XMMATRIX Components::Transform::GetNormalMat() const {
	using namespace DirectX;
	const XMMATRIX world = GetWorldMat();
	XMVECTOR determinant;
	const XMMATRIX inverse = XMMatrixInverse(&determinant, world);
	const float determinantValue = XMVectorGetX(determinant);
	if (!std::isfinite(determinantValue) || std::abs(determinantValue) < 1.0e-8f) {
		return XMMatrixIdentity();
	}
	return XMMatrixTranspose(inverse);
}

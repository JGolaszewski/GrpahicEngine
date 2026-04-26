#include "components/Transform.h"

void Components::Transform::SetPosition(float x, float y, float z) {
	Position = { x, y, z };
}

void Components::Transform::SetRotation(float pitch, float yaw, float roll) {
	DirectX::XMVECTOR quat = DirectX::XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);
    quat = DirectX::XMQuaternionNormalize(quat);
	DirectX::XMStoreFloat4(&Rotation, quat);
}

void Components::Transform::SetScale(float x, float y, float z) {
	Scale = { x, y, z };
}

void Components::Transform::Translate(float x, float y, float z) {
	Position.x += x;
	Position.y += y;
	Position.z += z;
}

void Components::Transform::ScaleBy(float x, float y, float z) {
	Scale.x *= x;
	Scale.y *= y;
	Scale.z *= z;
}

void Components::Transform::Rotate(float pitch, float yaw, float roll) {
	using namespace DirectX;

	XMVECTOR currentRot = XMLoadFloat4(&Rotation);

	XMVECTOR deltaRot = XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);

	XMVECTOR newRot = XMQuaternionMultiply(currentRot, deltaRot);
	newRot = XMQuaternionNormalize(newRot);

	XMStoreFloat4(&Rotation, newRot);
}

void Components::Transform::LookAt(float targetX, float targetY, float targetZ) {
	using namespace DirectX;

	XMVECTOR pos = XMLoadFloat3(&Position);
	XMVECTOR target = XMVectorSet(targetX, targetY, targetZ, 0.0f);

	XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(target, pos));

	if(XMVector3Equal(forward, XMVectorZero())) return;

	XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));
	XMVECTOR up = XMVector3Cross(forward, right);

	XMMATRIX rotMat = XMMatrixIdentity();
	rotMat.r[0] = right;
	rotMat.r[1] = up;
	rotMat.r[2] = forward;

	XMVECTOR quat = XMQuaternionRotationMatrix(rotMat);
	XMStoreFloat4(&Rotation, quat);
}

DirectX::XMMATRIX Components::Transform::GetWorldMat() const {
	using namespace DirectX;
	XMVECTOR pos = XMLoadFloat3(&Position);
	XMVECTOR rot = XMLoadFloat4(&Rotation);
	XMVECTOR scale = XMLoadFloat3(&Scale);

	return XMMatrixAffineTransformation(scale, XMVectorZero(), rot, pos);
}

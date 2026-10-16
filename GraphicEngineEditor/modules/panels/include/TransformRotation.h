#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Editor::TransformRotation {

/// Converts an XYZW quaternion to the inspector's pitch, yaw, roll values in degrees.
inline auto ToPitchYawRollDegrees(const std::array<float, 4>& quaternion) -> std::array<float, 3> {
  using namespace DirectX;
  const XMVECTOR rotation =
      XMQuaternionNormalize(XMVectorSet(quaternion[0], quaternion[1], quaternion[2], quaternion[3]));
  const XMVECTOR forward = XMVector3Rotate(XMVectorSet(0.0F, 0.0F, 1.0F, 0.0F), rotation);
  const float pitch = std::asin((std::clamp)(-XMVectorGetY(forward), -1.0F, 1.0F));
  const float yaw = std::atan2(XMVectorGetX(forward), XMVectorGetZ(forward));
  const XMVECTOR noRollRotation = XMQuaternionRotationRollPitchYaw(pitch, yaw, 0.0F);
  const XMVECTOR baseUp = XMVector3Rotate(XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F), noRollRotation);
  const XMVECTOR baseRight = XMVector3Rotate(XMVectorSet(1.0F, 0.0F, 0.0F, 0.0F), noRollRotation);
  const XMVECTOR actualUp = XMVector3Rotate(XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F), rotation);
  const float roll =
      std::atan2(-XMVectorGetX(XMVector3Dot(actualUp, baseRight)), XMVectorGetX(XMVector3Dot(actualUp, baseUp)));
  return {XMConvertToDegrees(pitch), XMConvertToDegrees(yaw), XMConvertToDegrees(roll)};
}

/// Converts inspector pitch, yaw, roll values in degrees to an XYZW quaternion.
inline auto FromPitchYawRollDegrees(const std::array<float, 3>& angles) -> std::array<float, 4> {
  DirectX::XMFLOAT4 result{};
  DirectX::XMStoreFloat4(&result, DirectX::XMQuaternionRotationRollPitchYaw(DirectX::XMConvertToRadians(angles[0]),
                                                                            DirectX::XMConvertToRadians(angles[1]),
                                                                            DirectX::XMConvertToRadians(angles[2])));
  return {result.x, result.y, result.z, result.w};
}

} // namespace Editor::TransformRotation

#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace Engine::Renderer {

/// Camera data used to fit a directional shadow map to the active receiver region.
struct DirectionalShadowCamera {
  DirectX::XMFLOAT3 Position{};
  DirectX::XMFLOAT3 Forward{0.0F, 0.0F, 1.0F};
  DirectX::XMFLOAT3 Right{1.0F, 0.0F, 0.0F};
  DirectX::XMFLOAT3 Up{0.0F, 1.0F, 0.0F};
  float VerticalFovRadians = DirectX::XM_PIDIV4;
  float AspectRatio = 1.0F;
  float NearPlane = 0.1F;
  float FarPlane = 100.0F;
};

/// Stable directional projection and its user-visible coverage measurements.
struct DirectionalShadowFit {
  DirectX::XMFLOAT4X4 ViewProjection{};
  DirectX::XMFLOAT3 CoverageCenter{};
  float CoverageWidth = 0.0F;
  float CoverageDepth = 0.0F;
  float TexelWorldSize = 0.0F;
  bool Valid = false;
};

/// Fits one orthographic shadow projection to camera frusta, with light-space XY texel snapping.
/// The receiver footprint comes only from these cameras; upstream casters receive bounded depth padding,
/// so distant geometry outside the camera footprint cannot reduce receiver detail.
[[nodiscard]] inline auto FitDirectionalShadow(std::span<const DirectionalShadowCamera> cameras,
                                               const DirectX::XMFLOAT3& lightRayDirection, uint32_t resolution,
                                               float maximumDistance, float upstreamCasterPadding) noexcept
    -> std::optional<DirectionalShadowFit> {
  using namespace DirectX;
  if (cameras.empty() || resolution < 8 || !std::isfinite(maximumDistance) || maximumDistance <= 0.0F ||
      !std::isfinite(upstreamCasterPadding) || upstreamCasterPadding < 0.0F) {
    return std::nullopt;
  }

  XMVECTOR ray = XMLoadFloat3(&lightRayDirection);
  const float rayLengthSquared = XMVectorGetX(XMVector3LengthSq(ray));
  if (!std::isfinite(rayLengthSquared) || rayLengthSquared < 1.0e-6F) {
    return std::nullopt;
  }
  ray = XMVector3Normalize(ray);

  XMVECTOR up = XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F);
  if (std::abs(XMVectorGetX(XMVector3Dot(up, ray))) > 0.98F) {
    up = XMVectorSet(1.0F, 0.0F, 0.0F, 0.0F);
  }
  XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, ray));
  up = XMVector3Normalize(XMVector3Cross(ray, right));

  XMFLOAT3 rightFloat{};
  XMFLOAT3 upFloat{};
  XMFLOAT3 rayFloat{};
  XMStoreFloat3(&rightFloat, right);
  XMStoreFloat3(&upFloat, up);
  XMStoreFloat3(&rayFloat, ray);

  float minX = (std::numeric_limits<float>::max)();
  float minY = (std::numeric_limits<float>::max)();
  float minZ = (std::numeric_limits<float>::max)();
  float maxX = -(std::numeric_limits<float>::max)();
  float maxY = -(std::numeric_limits<float>::max)();
  float maxZ = -(std::numeric_limits<float>::max)();
  XMVECTOR sum = XMVectorZero();
  size_t cornerCount = 0;

  const XMVECTOR rightAxis = XMLoadFloat3(&rightFloat);
  const XMVECTOR upAxis = XMLoadFloat3(&upFloat);
  const XMVECTOR rayAxis = XMLoadFloat3(&rayFloat);
  for (const DirectionalShadowCamera& camera : cameras) {
    const float aspect = camera.AspectRatio;
    const float fov = camera.VerticalFovRadians;
    const float nearPlane = camera.NearPlane;
    const float farPlane = (std::min)(camera.FarPlane, maximumDistance);
    if (!std::isfinite(aspect) || aspect <= 0.0F || !std::isfinite(fov) || fov <= 0.0F || fov >= XM_PI ||
        !std::isfinite(nearPlane) || nearPlane < 0.0F || !std::isfinite(farPlane) || farPlane <= nearPlane) {
      continue;
    }

    const XMVECTOR position = XMLoadFloat3(&camera.Position);
    const XMVECTOR rawForward = XMLoadFloat3(&camera.Forward);
    const XMVECTOR rawRight = XMLoadFloat3(&camera.Right);
    const XMVECTOR rawUp = XMLoadFloat3(&camera.Up);
    const float forwardLengthSquared = XMVectorGetX(XMVector3LengthSq(rawForward));
    const float rightLengthSquared = XMVectorGetX(XMVector3LengthSq(rawRight));
    const float upLengthSquared = XMVectorGetX(XMVector3LengthSq(rawUp));
    const bool finitePosition =
        std::isfinite(camera.Position.x) && std::isfinite(camera.Position.y) && std::isfinite(camera.Position.z);
    if (!finitePosition || !std::isfinite(forwardLengthSquared) || forwardLengthSquared < 1.0e-6F ||
        !std::isfinite(rightLengthSquared) || rightLengthSquared < 1.0e-6F || !std::isfinite(upLengthSquared) ||
        upLengthSquared < 1.0e-6F) {
      continue;
    }
    const XMVECTOR forward = XMVector3Normalize(rawForward);
    const XMVECTOR cameraRight = XMVector3Normalize(rawRight);
    const XMVECTOR cameraUp = XMVector3Normalize(rawUp);
    const float halfHeightScale = std::tan(fov * 0.5F);
    if (!std::isfinite(halfHeightScale) || halfHeightScale <= 0.0F) {
      continue;
    }

    for (float distance : {nearPlane, farPlane}) {
      const float halfHeight = distance * halfHeightScale;
      const float halfWidth = halfHeight * aspect;
      const XMVECTOR center = XMVectorAdd(position, XMVectorScale(forward, distance));
      for (float verticalSign : {-1.0F, 1.0F}) {
        for (float horizontalSign : {-1.0F, 1.0F}) {
          XMVECTOR point = XMVectorAdd(center, XMVectorScale(cameraRight, horizontalSign * halfWidth));
          point = XMVectorAdd(point, XMVectorScale(cameraUp, verticalSign * halfHeight));
          const float x = XMVectorGetX(XMVector3Dot(point, rightAxis));
          const float y = XMVectorGetX(XMVector3Dot(point, upAxis));
          const float z = XMVectorGetX(XMVector3Dot(point, rayAxis));
          if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            continue;
          }
          minX = (std::min)(minX, x);
          minY = (std::min)(minY, y);
          minZ = (std::min)(minZ, z);
          maxX = (std::max)(maxX, x);
          maxY = (std::max)(maxY, y);
          maxZ = (std::max)(maxZ, z);
          sum = XMVectorAdd(sum, point);
          ++cornerCount;
        }
      }
    }
  }
  if (cornerCount == 0 || !(maxX > minX) || !(maxY > minY) || !(maxZ > minZ)) {
    return std::nullopt;
  }

  const float centerX = (minX + maxX) * 0.5F;
  const float centerY = (minY + maxY) * 0.5F;
  // A square footprint preserves texel density in both light-space axes. The small guard band
  // keeps receiver corners covered after the center is snapped to the nearest shadow texel.
  float halfExtent = (std::max)(maxX - minX, maxY - minY) * 0.5F;
  halfExtent *= static_cast<float>(resolution) / static_cast<float>(resolution - 4U);
  const float texelWorldSize = (2.0F * halfExtent) / static_cast<float>(resolution);
  const float snappedX = std::round(centerX / texelWorldSize) * texelWorldSize;
  const float snappedY = std::round(centerY / texelWorldSize) * texelWorldSize;

  const float lightNear = 0.01F;
  const float lightMinZ = minZ - upstreamCasterPadding;
  const float depth = (maxZ - lightMinZ) + lightNear;
  const XMVECTOR lightEye = XMVectorAdd(XMVectorScale(rightAxis, snappedX), XMVectorScale(upAxis, snappedY));
  const XMVECTOR eye = XMVectorAdd(lightEye, XMVectorScale(rayAxis, lightMinZ - lightNear));
  const XMMATRIX view = XMMatrixLookToLH(eye, rayAxis, upAxis);
  const XMMATRIX projection =
      XMMatrixOrthographicOffCenterLH(-halfExtent, halfExtent, -halfExtent, halfExtent, lightNear, depth);
  const XMMATRIX viewProjection = view * projection;

  DirectionalShadowFit fit{};
  XMStoreFloat4x4(&fit.ViewProjection, viewProjection);
  const XMVECTOR centerWorld = XMVectorScale(sum, 1.0F / static_cast<float>(cornerCount));
  XMStoreFloat3(&fit.CoverageCenter, centerWorld);
  fit.CoverageWidth = halfExtent * 2.0F;
  fit.CoverageDepth = depth;
  fit.TexelWorldSize = texelWorldSize;
  fit.Valid = true;
  return fit;
}

} // namespace Engine::Renderer

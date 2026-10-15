#include "rendering/include/renderer/CameraView.h"

#include <DirectXMath.h>

#include <cmath>

namespace Engine::Renderer {

auto ResolveSceneCameraView(const Engine::Ecs::Camera& camera, const Engine::Ecs::WorldTransform& transform) noexcept
    -> std::optional<CameraView> {
  DirectX::XMVECTOR scale{};
  DirectX::XMVECTOR rotation{};
  DirectX::XMVECTOR position{};
  if (!DirectX::XMMatrixDecompose(&scale, &rotation, &position, DirectX::XMLoadFloat4x4(&transform.Matrix))) {
    return std::nullopt;
  }

  DirectX::XMFLOAT3 scaleValue{};
  DirectX::XMFLOAT4 quaternion{};
  DirectX::XMFLOAT3 translation{};
  DirectX::XMStoreFloat3(&scaleValue, scale);
  DirectX::XMStoreFloat4(&quaternion, rotation);
  DirectX::XMStoreFloat3(&translation, position);
  if (!std::isfinite(scaleValue.x) || !std::isfinite(scaleValue.y) || !std::isfinite(scaleValue.z) ||
      std::abs(scaleValue.x) < 1.0e-6F || std::abs(scaleValue.y) < 1.0e-6F || std::abs(scaleValue.z) < 1.0e-6F ||
      !std::isfinite(quaternion.x) || !std::isfinite(quaternion.y) || !std::isfinite(quaternion.z) ||
      !std::isfinite(quaternion.w) || !std::isfinite(translation.x) || !std::isfinite(translation.y) ||
      !std::isfinite(translation.z)) {
    return std::nullopt;
  }
  return CameraView{
      .Position = {translation.x, translation.y, translation.z},
      .Rotation = {quaternion.x, quaternion.y, quaternion.z, quaternion.w},
      .FieldOfViewDegrees = camera.FieldOfViewDegrees,
      .NearPlane = camera.NearPlane,
      .FarPlane = camera.FarPlane,
  };
}

} // namespace Engine::Renderer

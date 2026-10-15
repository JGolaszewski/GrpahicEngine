#include "rendering/include/renderer/render_graph/RenderView.h"
#include "rendering/include/renderer/Viewport.h"
#include "scene/include/Scene.h"

#include <algorithm>

namespace Engine::Renderer {

void RenderView::Initialize(const Engine::Scene* scene, Engine::EntityHandle cameraEntity,
                            const Engine::Viewport* viewport) {
  if (scene == nullptr || cameraEntity == Engine::InvalidEntityHandle || viewport == nullptr) {
    return;
  }

  const auto* camera = scene->World().Get<Engine::Ecs::Camera>(cameraEntity);
  const auto* transform = scene->World().Get<Engine::Ecs::WorldTransform>(cameraEntity);
  const auto* visibility = scene->World().Get<Engine::Ecs::GlobalVisibility>(cameraEntity);
  if (camera == nullptr || transform == nullptr || visibility == nullptr || !camera->Active || !visibility->IsVisible) {
    return;
  }

  ViewportWidth = viewport->GetWidth();
  ViewportHeight = viewport->GetHeight();

  TargetFramebuffer = viewport->GetFramebuffer();
  DirectX::XMVECTOR scale;
  DirectX::XMVECTOR rotation;
  DirectX::XMVECTOR position;
  if (!DirectX::XMMatrixDecompose(&scale, &rotation, &position, DirectX::XMLoadFloat4x4(&transform->Matrix))) {
    return;
  }
  const DirectX::XMVECTOR forward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0.0F, 0.0F, 1.0F, 0.0F), rotation);
  const DirectX::XMVECTOR up = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F), rotation);
  DirectX::XMStoreFloat3(&CameraPosition, position);

  float aspect = 1.0F;
  if (ViewportHeight > 0) {
    aspect = static_cast<float>(ViewportWidth) / static_cast<float>(ViewportHeight);
  }

  ViewMatrix = DirectX::XMMatrixLookToLH(position, forward, up);
  ProjectionMatrix = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(camera->FieldOfViewDegrees), aspect,
                                                       camera->NearPlane, camera->FarPlane);
  ViewProjection = ViewMatrix * ProjectionMatrix;
  ExtractFrustumPlanes();
}

void RenderView::ExtractFrustumPlanes() {
  const DirectX::XMMATRIX matrix = DirectX::XMMatrixTranspose(ViewProjection);
  FrustumPlanes[0] = DirectX::XMPlaneNormalize(DirectX::XMVectorAdd(matrix.r[3], matrix.r[0]));
  FrustumPlanes[1] = DirectX::XMPlaneNormalize(DirectX::XMVectorSubtract(matrix.r[3], matrix.r[0]));
  FrustumPlanes[2] = DirectX::XMPlaneNormalize(DirectX::XMVectorAdd(matrix.r[3], matrix.r[1]));
  FrustumPlanes[3] = DirectX::XMPlaneNormalize(DirectX::XMVectorSubtract(matrix.r[3], matrix.r[1]));
  FrustumPlanes[4] = DirectX::XMPlaneNormalize(matrix.r[2]);
  FrustumPlanes[5] = DirectX::XMPlaneNormalize(DirectX::XMVectorSubtract(matrix.r[3], matrix.r[2]));
}

auto RenderView::IsSphereVisible(const DirectX::XMFLOAT3& center, float radius) const -> bool {
  const DirectX::XMVECTOR point = DirectX::XMLoadFloat3(&center);
  const float safeRadius = (std::max)(radius, 0.0F);
  return std::ranges::all_of(FrustumPlanes, [point, safeRadius](const DirectX::XMVECTOR& plane) -> bool {
    return DirectX::XMVectorGetX(DirectX::XMPlaneDotCoord(plane, point)) >= -safeRadius;
  });
}

} // namespace Engine::Renderer

#include "rendering/src/render_graph/RenderView.h"
#include "rendering/include/ViewportTarget.h"
#include "rendering/include/renderer/Viewport.h"
#include "scene/include/Scene.h"

#include <algorithm>

namespace Engine::Renderer {

void RenderView::Initialize(const Engine::Scene* scene, const Engine::Renderer::CameraView& camera,
                            const Engine::Viewport* viewport) {
  if (scene == nullptr || viewport == nullptr) {
    return;
  }

  ViewportWidth = viewport->GetWidth();
  ViewportHeight = viewport->GetHeight();

  TargetFramebuffer = static_cast<nvrhi::IFramebuffer*>(viewport->GetSceneFramebufferHandle());
  DisplayFramebuffer = static_cast<nvrhi::IFramebuffer*>(viewport->GetFramebufferHandle());
  DisplayColorTexture = static_cast<nvrhi::ITexture*>(viewport->GetDisplayColorTextureHandle());
  if (viewport->m_Target != nullptr) {
    SnapshotOverdrawTexture = static_cast<nvrhi::ITexture*>(viewport->m_Target->GetOverdrawTextureHandle());
    SnapshotOverflowBuffer = static_cast<nvrhi::IBuffer*>(viewport->m_Target->GetSnapshotOverflowBufferHandle());
    SnapshotOverdrawFramebuffer = static_cast<nvrhi::IFramebuffer*>(viewport->m_Target->GetOverdrawFramebufferHandle());
  }
  const DirectX::XMFLOAT4 rotationValue{camera.Rotation[0], camera.Rotation[1], camera.Rotation[2], camera.Rotation[3]};
  const DirectX::XMFLOAT3 positionValue{camera.Position[0], camera.Position[1], camera.Position[2]};
  const DirectX::XMVECTOR rotation = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&rotationValue));
  const DirectX::XMVECTOR position = DirectX::XMLoadFloat3(&positionValue);
  const DirectX::XMVECTOR forward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0.0F, 0.0F, 1.0F, 0.0F), rotation);
  const DirectX::XMVECTOR up = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F), rotation);
  DirectX::XMStoreFloat3(&CameraPosition, position);

  float aspect = 1.0F;
  if (ViewportHeight > 0) {
    aspect = static_cast<float>(ViewportWidth) / static_cast<float>(ViewportHeight);
  }

  ViewMatrix = DirectX::XMMatrixLookToLH(position, forward, up);
  ProjectionMatrix = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(camera.FieldOfViewDegrees), aspect,
                                                       camera.NearPlane, camera.FarPlane);
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

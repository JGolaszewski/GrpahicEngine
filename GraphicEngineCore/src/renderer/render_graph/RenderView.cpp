#include "renderer/render_graph/RenderView.h"
#include "renderer/Camera.h"
#include "renderer/Viewport.h"

#include <algorithm>

namespace Renderer
{

void RenderView::Initialize(const Renderer::Camera *camera, const Engine::Viewport *viewport)
{
    if (!camera || !viewport)
        return;

    ViewportWidth = viewport->GetWidth();
    ViewportHeight = viewport->GetHeight();

    TargetFramebuffer = viewport->GetFramebuffer();
    DirectX::XMStoreFloat3(&CameraPosition, DirectX::XMVector3TransformCoord(
                                                DirectX::XMVectorZero(), camera->m_Transform.GetWorldMat()));

    float aspect = 1.0f;
    if (ViewportHeight > 0)
    {
        aspect = static_cast<float>(ViewportWidth) / static_cast<float>(ViewportHeight);
    }

    ViewMatrix = camera->GetView();
    ProjectionMatrix = camera->GetProjection(aspect);
    ViewProjection = ViewMatrix * ProjectionMatrix;
    ExtractFrustumPlanes();
}

void RenderView::ExtractFrustumPlanes()
{
    const DirectX::XMMATRIX matrix = DirectX::XMMatrixTranspose(ViewProjection);
    FrustumPlanes[0] = DirectX::XMPlaneNormalize(DirectX::XMVectorAdd(matrix.r[3], matrix.r[0]));
    FrustumPlanes[1] = DirectX::XMPlaneNormalize(DirectX::XMVectorSubtract(matrix.r[3], matrix.r[0]));
    FrustumPlanes[2] = DirectX::XMPlaneNormalize(DirectX::XMVectorAdd(matrix.r[3], matrix.r[1]));
    FrustumPlanes[3] = DirectX::XMPlaneNormalize(DirectX::XMVectorSubtract(matrix.r[3], matrix.r[1]));
    FrustumPlanes[4] = DirectX::XMPlaneNormalize(matrix.r[2]);
    FrustumPlanes[5] = DirectX::XMPlaneNormalize(DirectX::XMVectorSubtract(matrix.r[3], matrix.r[2]));
}

bool RenderView::IsSphereVisible(const DirectX::XMFLOAT3 &center, float radius) const
{
    const DirectX::XMVECTOR point = DirectX::XMLoadFloat3(&center);
    const float safeRadius = (std::max)(radius, 0.0f);
    for (const DirectX::XMVECTOR &plane : FrustumPlanes)
    {
        if (DirectX::XMVectorGetX(DirectX::XMPlaneDotCoord(plane, point)) < -safeRadius)
        {
            return false;
        }
    }
    return true;
}

} // namespace Renderer

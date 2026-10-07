#include "rendering/include/SceneRenderer.h"
#include "runtime/include/LogMacros.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

#include "scene/include/Scene.h"
#include "rendering/include/renderer/DebugGeometry.h"
#include "rendering/include/renderer/Entity.h"
#include "rendering/include/renderer/Material.h"
#include "rendering/include/renderer/Mesh.h"

namespace
{
struct DebugViewData
{
    DirectX::XMFLOAT4X4 ViewProjection;
};

DirectX::XMVECTOR NormalizeOrDefault(const DirectX::XMFLOAT3 &value)
{
    DirectX::XMVECTOR vector = DirectX::XMLoadFloat3(&value);
    const float lengthSquared = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(vector));
    if (!std::isfinite(lengthSquared) || lengthSquared < 1e-6f)
    {
        return DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
    }
    return DirectX::XMVector3Normalize(vector);
}

static_assert(sizeof(::Engine::Renderer::PerObjectGpuData) == Engine::Scene::Engine::Renderer::PerObjectDataStride);

DirectX::XMMATRIX ComputeSpotLightViewProj(const ::Engine::Renderer::DirectionalLightData &lightData)
{
    using namespace DirectX;

    XMFLOAT3 position =
        XMFLOAT3(lightData.PositionRange.x, lightData.PositionRange.y, lightData.PositionRange.z);
    XMFLOAT3 direction = XMFLOAT3(lightData.DirectionIntensity.x, lightData.DirectionIntensity.y,
                                  lightData.DirectionIntensity.z);

    XMVECTOR dir = XMVector3Normalize(XMLoadFloat3(&direction));
    if (XMVectorGetX(XMVector3LengthSq(dir)) < 1e-6f)
    {
        dir = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
    }

    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const float upDot = std::abs(XMVectorGetX(XMVector3Dot(up, dir)));
    if (upDot > 0.98f)
    {
        up = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    }

    XMVECTOR pos = XMLoadFloat3(&position);
    XMMATRIX view = XMMatrixLookToLH(pos, dir, up);

    const float outerCos = std::clamp(lightData.ConeParams.y, -1.0f, 1.0f);
    const float outerAngle = std::acos(outerCos);
    const float fovY = std::clamp(outerAngle * 2.0f, 0.2f, DirectX::XM_PI - 0.01f);
    const float nearPlane = 0.05f;
    const float farPlane = std::max(lightData.PositionRange.w, nearPlane + 0.01f);

    XMMATRIX proj = XMMatrixPerspectiveFovLH(fovY, 1.0f, nearPlane, farPlane);
    return view * proj;
}
} // namespace

// --- Initialization ---

Engine::Scene::Engine::Renderer::SceneRenderer(nvrhi::IDevice *device)
{
}

void Engine::Scene::Engine::Renderer::Init(nvrhi::IDevice *device, ::Engine::Renderer::MaterialSystem *materialSystem,
                                 ::Engine::Renderer::ShaderFactory *shaderFactory)
{
    m_MaterialSystem = materialSystem;
    m_PipelineCache.Init(device);

    m_Device = device;

    if (shaderFactory)
    {
        m_DebugVS = shaderFactory->GetShader("shaders/debug_vs.dxbc", nvrhi::ShaderType::Vertex);
        m_DebugPS = shaderFactory->GetShader("shaders/debug_ps.dxbc", nvrhi::ShaderType::Pixel);
        m_ShadowMaskPS = shaderFactory->GetShader("shaders/shadow_mask_ps.dxbc", nvrhi::ShaderType::Pixel);
    }

    nvrhi::BindingLayoutDesc perObjectLayoutDesc;
    perObjectLayoutDesc.visibility = nvrhi::ShaderType::All;
    perObjectLayoutDesc.bindings = {nvrhi::BindingLayoutItem::ConstantBuffer(0)};
    m_PerObjectBindingLayout = device->createBindingLayout(perObjectLayoutDesc);

    nvrhi::BindingLayoutDesc sceneLayoutDesc;
    sceneLayoutDesc.visibility = nvrhi::ShaderType::All;
    sceneLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::ConstantBuffer(2), nvrhi::BindingLayoutItem::Texture_SRV(6),
        nvrhi::BindingLayoutItem::Sampler(6), nvrhi::BindingLayoutItem::ConstantBuffer(4)};
    m_SceneBindingLayout = device->createBindingLayout(sceneLayoutDesc);

    nvrhi::BindingLayoutDesc debugLayoutDesc;
    debugLayoutDesc.visibility = nvrhi::ShaderType::Vertex;
    debugLayoutDesc.bindings = {nvrhi::BindingLayoutItem::ConstantBuffer(0)};
    m_DebugBindingLayout = device->createBindingLayout(debugLayoutDesc);

    nvrhi::BufferDesc debugViewDesc;
    debugViewDesc.byteSize = sizeof(DebugViewData);
    debugViewDesc.isConstantBuffer = true;
    debugViewDesc.isVolatile = true;
    debugViewDesc.maxVersions = 8;
    debugViewDesc.debugName = "DebugView_Buffer";
    m_DebugViewBuffer = device->createBuffer(debugViewDesc);

    nvrhi::SamplerDesc shadowSamplerDesc;
    shadowSamplerDesc.setAddressU(nvrhi::SamplerAddressMode::Border)
        .setAddressV(nvrhi::SamplerAddressMode::Border)
        .setAddressW(nvrhi::SamplerAddressMode::Border)
        .setMinFilter(true)
        .setMagFilter(true)
        .setMipFilter(false)
        .setReductionType(nvrhi::SamplerReductionType::Comparison)
        .setBorderColor(nvrhi::Color(1.0f, 1.0f, 1.0f, 1.0f));
    m_ShadowSampler = device->createSampler(shadowSamplerDesc);
}

void Engine::Scene::Engine::Renderer::EnsureDebugBuffer(nvrhi::BufferHandle &buffer, uint64_t &capacity,
                                              size_t vertexCount, const char *debugName)
{
    if (vertexCount == 0)
    {
        return;
    }

    const uint64_t requiredBytes = static_cast<uint64_t>(vertexCount) * sizeof(::Engine::Renderer::DebugVertex);
    if (buffer && capacity >= requiredBytes)
    {
        return;
    }

    uint64_t newCapacity =
        (std::max)(requiredBytes, static_cast<uint64_t>(sizeof(::Engine::Renderer::DebugVertex) * 256));
    while (newCapacity < requiredBytes && newCapacity <= UINT64_MAX / 2)
    {
        newCapacity *= 2;
    }

    nvrhi::BufferDesc desc;
    desc.byteSize = newCapacity;
    desc.isVertexBuffer = true;
    desc.initialState = nvrhi::ResourceStates::VertexBuffer;
    desc.keepInitialState = true;
    desc.debugName = debugName ? debugName : "DebugGeometry_Buffer";
    nvrhi::BufferHandle newBuffer = m_Device ? m_Device->createBuffer(desc) : nullptr;
    if (newBuffer)
    {
        buffer = std::move(newBuffer);
        capacity = newCapacity;
    }
}

nvrhi::GraphicsPipelineHandle Engine::Scene::Engine::Renderer::GetDebugPipeline(
    const nvrhi::FramebufferInfo &framebuffer, nvrhi::PrimitiveType primitiveType,
    ::Engine::Renderer::DebugDepthMode depthMode)
{
    if (!m_DebugVS || !m_DebugPS || !m_DebugBindingLayout)
    {
        return nullptr;
    }

    Engine::PipelineKey key;
    key.VS = m_DebugVS;
    key.PS = m_DebugPS;
    key.PerObjectLayout = m_DebugBindingLayout;
    key.PrimType = primitiveType;
    key.InputLayout = {nvrhi::VertexAttributeDesc()
                           .setName("POSITION")
                           .setFormat(nvrhi::Format::RGB32_FLOAT)
                           .setOffset(offsetof(::Engine::Renderer::DebugVertex, Position))
                           .setElementStride(sizeof(::Engine::Renderer::DebugVertex)),
                       nvrhi::VertexAttributeDesc()
                           .setName("COLOR")
                           .setFormat(nvrhi::Format::RGBA32_FLOAT)
                           .setOffset(offsetof(::Engine::Renderer::DebugVertex, Color))
                           .setElementStride(sizeof(::Engine::Renderer::DebugVertex))};
    key.DepthTest = depthMode == ::Engine::Renderer::DebugDepthMode::Test;
    key.DepthWrite = false;
    key.CullMode = nvrhi::RasterCullMode::None;
    key.RasterState.setCullNone();
    key.RasterState.setAntialiasedLineEnable(primitiveType == nvrhi::PrimitiveType::LineList);

    nvrhi::BlendState::RenderTarget blendTarget;
    blendTarget.enableBlend()
        .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
        .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
        .setSrcBlendAlpha(nvrhi::BlendFactor::One)
        .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
    key.BlendState.setRenderTarget(0, blendTarget);

    return m_PipelineCache.GetOrCreatePipeline(key, framebuffer);
}

void Engine::Scene::Engine::Renderer::RenderDebugGeometry(nvrhi::ICommandList *commandList,
                                                const ::Engine::Renderer::RenderView &view)
{
    if (!commandList || !view.TargetFramebuffer || view.DebugGeometryData.Empty() || !m_DebugViewBuffer ||
        !m_DebugBindingLayout)
    {
        return;
    }

    const ::Engine::Renderer::DebugGeometry &geometry = view.DebugGeometryData;
    EnsureDebugBuffer(m_DebugLineBuffer, m_DebugLineBufferCapacity, geometry.GetLineVertices().size(),
                      "DebugLineVertices");
    EnsureDebugBuffer(m_DebugTriangleBuffer, m_DebugTriangleBufferCapacity,
                      geometry.GetTriangleVertices().size(), "DebugTriangleVertices");

    if ((!geometry.GetLineVertices().empty() && !m_DebugLineBuffer) ||
        (!geometry.GetTriangleVertices().empty() && !m_DebugTriangleBuffer))
    {
        return;
    }

    DebugViewData viewData;
    DirectX::XMStoreFloat4x4(&viewData.ViewProjection, DirectX::XMMatrixTranspose(view.ViewProjection));
    commandList->writeBuffer(m_DebugViewBuffer, &viewData, sizeof(viewData));

    nvrhi::BindingSetDesc bindingDesc;
    bindingDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_DebugViewBuffer));
    nvrhi::BindingSetHandle bindings = m_Device->createBindingSet(bindingDesc, m_DebugBindingLayout);
    if (!bindings)
    {
        return;
    }

    const nvrhi::FramebufferInfo framebufferInfo = view.TargetFramebuffer->getFramebufferInfo();
    const nvrhi::Viewport viewport =
        nvrhi::Viewport(static_cast<float>(view.ViewportWidth), static_cast<float>(view.ViewportHeight));
    const nvrhi::Rect scissor = nvrhi::Rect(view.ViewportWidth, view.ViewportHeight);

    const auto drawVertices = [&](const std::vector<::Engine::Renderer::DebugVertex> &vertices,
                                  nvrhi::BufferHandle vertexBuffer, nvrhi::PrimitiveType primitiveType) {
        if (vertices.empty() || !vertexBuffer)
        {
            return;
        }

        commandList->writeBuffer(vertexBuffer, vertices.data(),
                                 vertices.size() * sizeof(::Engine::Renderer::DebugVertex));
        nvrhi::GraphicsPipelineHandle pipeline =
            GetDebugPipeline(framebufferInfo, primitiveType, view.DebugSettings.DepthMode);
        if (!pipeline)
        {
            return;
        }

        nvrhi::GraphicsState state;
        state.pipeline = pipeline;
        state.framebuffer = view.TargetFramebuffer;
        state.viewport.addViewport(viewport);
        state.viewport.addScissorRect(scissor);
        state.bindings = {bindings};
        state.vertexBuffers = {nvrhi::VertexBufferBinding{vertexBuffer, 0, 0}};
        commandList->setGraphicsState(state);

        nvrhi::DrawArguments arguments;
        arguments.vertexCount = static_cast<uint32_t>(vertices.size());
        arguments.instanceCount = 1;
        commandList->draw(arguments);
    };

    drawVertices(geometry.GetTriangleVertices(), m_DebugTriangleBuffer, nvrhi::PrimitiveType::TriangleList);
    drawVertices(geometry.GetLineVertices(), m_DebugLineBuffer, nvrhi::PrimitiveType::LineList);
}

std::optional<Engine::LightId> Engine::Scene::Engine::Renderer::PickLight(const ::Engine::Renderer::RenderView &view,
                                                                float viewportX, float viewportY) const
{
    if (view.ViewportWidth == 0 || view.ViewportHeight == 0 || viewportX < 0.0f || viewportY < 0.0f ||
        viewportX >= static_cast<float>(view.ViewportWidth) ||
        viewportY >= static_cast<float>(view.ViewportHeight))
    {
        return std::nullopt;
    }

    const float ndcX = viewportX / static_cast<float>(view.ViewportWidth) * 2.0f - 1.0f;
    const float ndcY = 1.0f - viewportY / static_cast<float>(view.ViewportHeight) * 2.0f;
    const DirectX::XMMATRIX inverseViewProjection = DirectX::XMMatrixInverse(nullptr, view.ViewProjection);
    const DirectX::XMVECTOR clipNear = DirectX::XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
    const DirectX::XMVECTOR clipFar = DirectX::XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);
    DirectX::XMVECTOR rayOrigin = DirectX::XMVector4Transform(clipNear, inverseViewProjection);
    DirectX::XMVECTOR rayEnd = DirectX::XMVector4Transform(clipFar, inverseViewProjection);
    const float originW = DirectX::XMVectorGetW(rayOrigin);
    const float endW = DirectX::XMVectorGetW(rayEnd);
    if (!std::isfinite(originW) || !std::isfinite(endW) || std::abs(originW) < 1e-5f ||
        std::abs(endW) < 1e-5f)
    {
        return std::nullopt;
    }

    rayOrigin = DirectX::XMVectorScale(rayOrigin, 1.0f / originW);
    rayEnd = DirectX::XMVectorScale(rayEnd, 1.0f / endW);
    const DirectX::XMVECTOR rayDirection =
        DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(rayEnd, rayOrigin));
    const auto &targets = view.DebugGeometryData.GetLightTargets();
    float closestDistance = std::numeric_limits<float>::max();
    Engine::LightId closestLight = Engine::InvalidLightId;

    for (const ::Engine::Renderer::DebugLightTarget &target : targets)
    {
        const DirectX::XMVECTOR axis = NormalizeOrDefault(target.Direction);
        const DirectX::XMVECTOR offset =
            DirectX::XMVectorSubtract(rayOrigin, DirectX::XMLoadFloat3(&target.Position));
        const float axisRayDot = DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, rayDirection));
        const float axisOffsetDot = DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, offset));

        const float safeRange = std::max(target.Range, 0.25f);
        const float coneSlope =
            std::tan(std::clamp(target.OuterAngleRadians, 0.0f, DirectX::XM_PIDIV2 - 0.001f));
        const float quadraticA = 1.0f - axisRayDot * axisRayDot * (1.0f + coneSlope * coneSlope);
        const float quadraticB = 2.0f * (DirectX::XMVectorGetX(DirectX::XMVector3Dot(offset, rayDirection)) -
                                         axisOffsetDot * axisRayDot * (1.0f + coneSlope * coneSlope));
        const float quadraticC = DirectX::XMVectorGetX(DirectX::XMVector3Dot(offset, offset)) -
                                 axisOffsetDot * axisOffsetDot * (1.0f + coneSlope * coneSlope);

        float hitDistance = std::numeric_limits<float>::max();
        if (std::abs(quadraticA) < 1e-6f)
        {
            if (std::abs(quadraticB) > 1e-6f)
            {
                hitDistance = -quadraticC / quadraticB;
            }
        }
        else
        {
            const float discriminant = quadraticB * quadraticB - 4.0f * quadraticA * quadraticC;
            if (discriminant >= 0.0f)
            {
                const float sqrtDiscriminant = std::sqrt(discriminant);
                const float rootA = (-quadraticB - sqrtDiscriminant) / (2.0f * quadraticA);
                const float rootB = (-quadraticB + sqrtDiscriminant) / (2.0f * quadraticA);
                if (rootA > 0.0f)
                    hitDistance = rootA;
                if (rootB > 0.0f)
                    hitDistance = std::min(hitDistance, rootB);
            }
        }

        if (hitDistance == std::numeric_limits<float>::max())
        {
            const float sphereRadius = std::max(0.2f, safeRange * 0.025f);
            const float sphereA = DirectX::XMVectorGetX(DirectX::XMVector3Dot(rayDirection, rayDirection));
            const float sphereB = 2.0f * DirectX::XMVectorGetX(DirectX::XMVector3Dot(offset, rayDirection));
            const float sphereC =
                DirectX::XMVectorGetX(DirectX::XMVector3Dot(offset, offset)) - sphereRadius * sphereRadius;
            const float sphereDiscriminant = sphereB * sphereB - 4.0f * sphereA * sphereC;
            if (sphereDiscriminant >= 0.0f)
            {
                const float sphereRoot = (-sphereB - std::sqrt(sphereDiscriminant)) / (2.0f * sphereA);
                if (sphereRoot > 0.0f)
                    hitDistance = sphereRoot;
            }
        }

        if (hitDistance <= 0.0f || hitDistance >= std::numeric_limits<float>::max())
        {
            continue;
        }

        const DirectX::XMVECTOR hitPoint =
            DirectX::XMVectorAdd(rayOrigin, DirectX::XMVectorScale(rayDirection, hitDistance));
        const float hitAxialDistance = DirectX::XMVectorGetX(DirectX::XMVector3Dot(
            DirectX::XMVectorSubtract(hitPoint, DirectX::XMLoadFloat3(&target.Position)), axis));
        if (hitAxialDistance < -0.25f || hitAxialDistance > safeRange + 0.25f)
        {
            continue;
        }

        if (hitDistance < closestDistance)
        {
            closestDistance = hitDistance;
            closestLight = target.Id;
        }
    }

    return closestLight == Engine::InvalidLightId ? std::nullopt
                                                  : std::optional<Engine::LightId>(closestLight);
}

// --- Uploads ---

size_t Engine::Scene::Engine::Renderer::EstimateDrawCount(const ::Engine::Renderer::FrameBlackboard &blackboard) const
{
    size_t count = 0;
    const auto accumulate = [&count](const std::vector<::Engine::Renderer::RenderView *> &views) {
        for (const ::Engine::Renderer::RenderView *view : views)
        {
            if (!view || !view->TargetScene)
                continue;
            for (const auto &entity : view->TargetScene->GetEntities())
            {
                if (!entity)
                    continue;
                const size_t submeshCount = entity->Submeshes.empty() ? 1 : entity->Submeshes.size();
                if (submeshCount > std::numeric_limits<size_t>::max() - count)
                {
                    count = std::numeric_limits<size_t>::max();
                    return;
                }
                count += submeshCount;
            }
        }
    };
    accumulate(blackboard.GetShadowViews());
    accumulate(blackboard.GetSceneViews());
    return count;
}

bool Engine::Scene::Engine::Renderer::EnsureLightBuffers(size_t viewCount, ::Engine::Renderer::FrameContext &frameContext)
{
    for (size_t viewIndex = 0; viewIndex < viewCount; ++viewIndex)
    {
        if (!frameContext.GetOrCreateUploadBuffer(
                "PerViewLights/" + std::to_string(viewIndex), sizeof(::Engine::Renderer::LightsCBData),
                static_cast<uint32_t>(::Engine::Renderer::FrameContext::FramesInFlight + 1)))
            return false;
    }
    return true;
}

nvrhi::BufferHandle Engine::Scene::Engine::Renderer::GetLightBuffer(size_t viewIndex,
                                                          ::Engine::Renderer::FrameContext &frameContext) const
{
    return frameContext.GetOrCreateUploadBuffer(
        "PerViewLights/" + std::to_string(viewIndex), sizeof(::Engine::Renderer::LightsCBData),
        static_cast<uint32_t>(::Engine::Renderer::FrameContext::FramesInFlight + 1));
}

nvrhi::BindingSetHandle Engine::Scene::Engine::Renderer::GetSceneBindingSet(size_t viewIndex,
                                                                  nvrhi::BufferHandle lightBuffer,
                                                                  nvrhi::TextureHandle shadowMap,
                                                                  nvrhi::SamplerHandle shadowSampler,
                                                                  nvrhi::BufferHandle cameraBuffer,
                                                                  ::Engine::Renderer::FrameDiagnostics &diagnostics)
{
    if (!m_Device || !lightBuffer || !shadowMap || !shadowSampler || !cameraBuffer || !m_SceneBindingLayout)
        return nullptr;
    if (m_SceneBindingCaches.size() <= viewIndex)
        m_SceneBindingCaches.resize(viewIndex + 1);
    auto &cache = m_SceneBindingCaches[viewIndex];
    if (cache.Bindings && cache.LightBuffer == lightBuffer && cache.ShadowMap == shadowMap &&
        cache.ShadowSampler == shadowSampler && cache.CameraBuffer == cameraBuffer &&
        cache.Layout == m_SceneBindingLayout)
    {
        return cache.Bindings;
    }

    nvrhi::BindingSetDesc desc;
    desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(2, lightBuffer));
    desc.addItem(nvrhi::BindingSetItem::Texture_SRV(6, shadowMap));
    desc.addItem(nvrhi::BindingSetItem::Sampler(6, shadowSampler));
    desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(4, cameraBuffer));
    nvrhi::BindingSetHandle bindings = m_Device->createBindingSet(desc, m_SceneBindingLayout);
    if (!bindings)
        return nullptr;
    cache.LightBuffer = std::move(lightBuffer);
    cache.ShadowMap = std::move(shadowMap);
    cache.ShadowSampler = std::move(shadowSampler);
    cache.CameraBuffer = std::move(cameraBuffer);
    cache.Layout = m_SceneBindingLayout;
    cache.Bindings = std::move(bindings);
    ++diagnostics.BindingSetAllocations;
    return cache.Bindings;
}

bool Engine::Scene::Engine::Renderer::EnsurePerObjectResources(size_t maxDrawCount,
                                                     ::Engine::Renderer::FrameContext &frameContext,
                                                     ::Engine::Renderer::FrameDiagnostics &diagnostics)
{
    maxDrawCount = (std::max)(maxDrawCount, size_t{1});
    if (maxDrawCount <= m_PerObjectBufferCapacity && m_PerObjectBuffer && m_PerObjectBindingSet)
        return true;
    if (maxDrawCount > std::numeric_limits<uint32_t>::max())
        return false;

    nvrhi::BufferHandle buffer = frameContext.GetOrCreateUploadBuffer(
        "PerObjectDataRing", static_cast<uint32_t>(PerObjectDataStride), static_cast<uint32_t>(maxDrawCount));
    if (!buffer)
        return false;

    nvrhi::BindingSetDesc setDesc;
    setDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, buffer));
    nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(setDesc, m_PerObjectBindingLayout);
    if (!bindingSet)
        return false;

    m_PerObjectBuffer = std::move(buffer);
    m_PerObjectBindingSet = std::move(bindingSet);
    m_PerObjectBufferCapacity = maxDrawCount;
    ++diagnostics.BindingSetAllocations;
    return true;
}

void Engine::Scene::Engine::Renderer::CullAndFillView(::Engine::Renderer::RenderView *view)
{
    if (!view || !view->TargetScene)
        return;

    DirectX::XMMATRIX viewProj = view->ViewProjection;
    nvrhi::IFramebuffer *framebuffer = view->TargetFramebuffer; // FIXME

    nvrhi::FramebufferInfo fbInfo;
    if (framebuffer)
    {
        fbInfo = framebuffer->getFramebufferInfo();
    }
    else if (view->ViewType == ::Engine::Renderer::RenderView::Type::Shadow)
    {
        fbInfo.depthFormat = nvrhi::Format::D32;
    }
    else
    {
        return;
    }

    for (const auto &entity : view->TargetScene->GetEntities())
    {
        if (!entity || (entity->Submeshes.empty() && (!entity->mesh || !entity->material)))
        {
            ++m_SubmissionStats.ObjectsCulled;
            continue;
        }

        if (view->ViewType == ::Engine::Renderer::RenderView::Type::Shadow && !entity->CastShadows)
        {
            ++m_SubmissionStats.ObjectsCulled;
            continue;
        }

        const auto submitSubmesh = [&](const ::Engine::Renderer::Submesh &submesh) {
            if (!submesh.mesh || !submesh.material)
            {
                ++m_SubmissionStats.ObjectsCulled;
                return;
            }

            const auto cullingStart = std::chrono::steady_clock::now();
            const DirectX::XMMATRIX local = DirectX::XMLoadFloat4x4(&submesh.LocalTransform);
            const DirectX::XMMATRIX world = local * entity->transform.GetWorldMat();
            DirectX::XMFLOAT4X4 worldTransform;
            DirectX::XMStoreFloat4x4(&worldTransform, world);
            const bool boundsCacheMatches =
                submesh.WorldBoundsCached &&
                std::memcmp(&worldTransform, &submesh.CachedWorldTransform, sizeof(worldTransform)) == 0 &&
                std::memcmp(&submesh.BoundsMin, &submesh.CachedBoundsMin, sizeof(submesh.BoundsMin)) == 0 &&
                std::memcmp(&submesh.BoundsMax, &submesh.CachedBoundsMax, sizeof(submesh.BoundsMax)) == 0;
            if (!boundsCacheMatches)
            {
                const DirectX::XMFLOAT3 localCenter = {(submesh.BoundsMin.x + submesh.BoundsMax.x) * 0.5f,
                                                       (submesh.BoundsMin.y + submesh.BoundsMax.y) * 0.5f,
                                                       (submesh.BoundsMin.z + submesh.BoundsMax.z) * 0.5f};
                const DirectX::XMVECTOR center =
                    DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&localCenter), world);
                float radius = 0.0f;
                for (uint32_t corner = 0; corner < 8; ++corner)
                {
                    const DirectX::XMFLOAT3 point = {(corner & 1) ? submesh.BoundsMax.x : submesh.BoundsMin.x,
                                                     (corner & 2) ? submesh.BoundsMax.y : submesh.BoundsMin.y,
                                                     (corner & 4) ? submesh.BoundsMax.z
                                                                  : submesh.BoundsMin.z};
                    const DirectX::XMVECTOR worldPoint =
                        DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&point), world);
                    radius = (std::max)(radius, DirectX::XMVectorGetX(DirectX::XMVector3Length(
                                                    DirectX::XMVectorSubtract(worldPoint, center))));
                }
                DirectX::XMStoreFloat3(&submesh.CachedWorldBoundsCenter, center);
                submesh.CachedWorldBoundsRadius = radius;
                submesh.CachedWorldTransform = worldTransform;
                submesh.CachedBoundsMin = submesh.BoundsMin;
                submesh.CachedBoundsMax = submesh.BoundsMax;
                submesh.WorldBoundsCached = true;
            }
            const bool visible =
                view->IsSphereVisible(submesh.CachedWorldBoundsCenter, submesh.CachedWorldBoundsRadius);
            m_SubmissionStats.CullingTimeMs +=
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cullingStart)
                    .count();
            if (!visible)
            {
                ++m_SubmissionStats.ObjectsCulled;
                return;
            }

            const auto packetBuildStart = std::chrono::steady_clock::now();
            ::Engine::Renderer::PerObjectGpuData perObjectData{};
            const DirectX::XMMATRIX mvp = world * viewProj;
            DirectX::XMVECTOR determinant;
            const DirectX::XMMATRIX inverseWorld = DirectX::XMMatrixInverse(&determinant, world);
            const DirectX::XMMATRIX normal = std::abs(DirectX::XMVectorGetX(determinant)) > 1.0e-8f
                                                 ? DirectX::XMMatrixTranspose(inverseWorld)
                                                 : DirectX::XMMatrixIdentity();
            DirectX::XMStoreFloat4x4(&perObjectData.World, DirectX::XMMatrixTranspose(world));
            DirectX::XMStoreFloat4x4(&perObjectData.MVP, DirectX::XMMatrixTranspose(mvp));
            DirectX::XMStoreFloat4x4(&perObjectData.Normal, DirectX::XMMatrixTranspose(normal));
            perObjectData.ObjectParams = {entity->ReceiveShadows ? 1.0f : 0.0f,
                                          entity->ForceWireframe ? 1.0f : 0.0f, 0.0f, 0.0f};

            auto packet = CreateRenderPacket(submesh.mesh.get(), submesh.material.get());
            packet.PerObjectData = perObjectData;
            const ::Engine::Renderer::MaterialProperties materialProperties = submesh.material->GetProperties();
            if (view->ViewType == ::Engine::Renderer::RenderView::Type::Shadow &&
                materialProperties.AlphaMode == ::Engine::Renderer::MaterialAlphaMode::Blend)
            {
                return;
            }
            packet.ShadowAlphaTest = view->ViewType == ::Engine::Renderer::RenderView::Type::Shadow &&
                                     materialProperties.AlphaMode == ::Engine::Renderer::MaterialAlphaMode::Mask;
            packet.Queue = view->ViewType == ::Engine::Renderer::RenderView::Type::Shadow
                               ? ::Engine::Renderer::RenderQueueType::Opaque
                           : materialProperties.AlphaMode == ::Engine::Renderer::MaterialAlphaMode::Mask
                               ? ::Engine::Renderer::RenderQueueType::Masked
                           : materialProperties.AlphaMode == ::Engine::Renderer::MaterialAlphaMode::Blend
                               ? ::Engine::Renderer::RenderQueueType::Transparent
                               : ::Engine::Renderer::RenderQueueType::Opaque;
            packet.CameraDepth = DirectX::XMVectorGetZ(DirectX::XMVector3TransformCoord(
                DirectX::XMLoadFloat3(&submesh.CachedWorldBoundsCenter), view->ViewMatrix));
            ++m_SubmissionStats.ObjectsSubmitted;
            m_SubmissionStats.PerObjectBytesUsed += PerObjectDataStride;

            Engine::PipelineKey key;
            key.PrimType = submesh.mesh->GetPrimitiveType();
            key.VS = m_MaterialSystem->GetStandardVS();
            key.PerObjectLayout = m_PerObjectBindingLayout;
            key.SceneLayout = m_SceneBindingLayout;
            key.MaterialLayout = m_MaterialSystem->GetStandardMaterialLayout();
            key.DepthWrite = packet.Queue != ::Engine::Renderer::RenderQueueType::Transparent;
            if (view->ViewType == ::Engine::Renderer::RenderView::Type::Shadow)
            {
                key.PS = packet.ShadowAlphaTest ? m_ShadowMaskPS : nullptr;
                key.SceneLayout = nullptr;
                key.MaterialLayout =
                    packet.ShadowAlphaTest ? m_MaterialSystem->GetStandardMaterialLayout() : nullptr;
                key.Wireframe = false;
                key.DepthTest = true;
                key.CullMode = materialProperties.DoubleSided ? nvrhi::RasterCullMode::None
                                                              : nvrhi::RasterCullMode::Back;
            }
            else
            {
                key.PS = submesh.material && submesh.material->HasTextureOverrides()
                             ? m_MaterialSystem->GetTexturedPS()
                             : m_MaterialSystem->GetStandardPS();
                key.Wireframe = entity->ForceWireframe;
                key.DepthTest = !entity->DisableDepthTest;
                key.CullMode = entity->ForceWireframe || materialProperties.DoubleSided
                                   ? nvrhi::RasterCullMode::None
                                   : entity->CullMode;
                if (packet.Queue == ::Engine::Renderer::RenderQueueType::Transparent)
                {
                    nvrhi::BlendState::RenderTarget blendTarget;
                    blendTarget.enableBlend()
                        .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
                        .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
                        .setSrcBlendAlpha(nvrhi::BlendFactor::One)
                        .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
                    key.BlendState.setRenderTarget(0, blendTarget);
                }
            }
            packet.Pipeline = packet.ShadowAlphaTest && !m_ShadowMaskPS
                                  ? nullptr
                                  : m_PipelineCache.GetOrCreatePipeline(key, fbInfo);
            switch (packet.Queue)
            {
            case ::Engine::Renderer::RenderQueueType::Masked:
                view->Queues.Masked.push_back(packet);
                ++m_SubmissionStats.MaskedPackets;
                break;
            case ::Engine::Renderer::RenderQueueType::Transparent:
                view->Queues.Transparent.push_back(packet);
                ++m_SubmissionStats.TransparentPackets;
                break;
            default:
                view->Queues.Opaque.push_back(packet);
                ++m_SubmissionStats.OpaquePackets;
                break;
            }
            ++m_SubmissionStats.PacketsBuilt;
            m_SubmissionStats.PacketBuildTimeMs +=
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - packetBuildStart)
                    .count();
        };

        if (entity->Submeshes.empty())
        {
            ::Engine::Renderer::Submesh submesh;
            submesh.mesh = entity->mesh;
            submesh.material = entity->material;
            submesh.BoundsMin = {-1.0f, -1.0f, -1.0f};
            submesh.BoundsMax = {1.0f, 1.0f, 1.0f};
            submitSubmesh(submesh);
        }
        else
        {
            for (const auto &submesh : entity->Submeshes)
                submitSubmesh(submesh);
        }
    }
}

// --- RenderPacket creation ---

::Engine::Renderer::RenderPacket Engine::Scene::Engine::Renderer::CreateRenderPacket(const ::Engine::Renderer::Mesh *mesh,
                                                                 const ::Engine::Renderer::Material *material)
{
    ::Engine::Renderer::RenderPacket packet;
    packet.VertexBuffer = mesh->GetVertexBuffer();
    packet.IndexBuffer = mesh->GetIndexBuffer();
    packet.VertexCount = mesh->GetVertexCount();
    packet.IndexCount = mesh->GetIndexCount();
    packet.MaterialBindings = material->GetBindingSet();
    packet.BaseVertexOffset = 0;
    packet.StartIndexLocation = 0;

    return packet;
}

void Engine::Scene::Engine::Renderer::SortViewQueues(::Engine::Renderer::RenderView *view)
{
    if (!view)
        return;

    const auto lessBySubmissionState = [](const ::Engine::Renderer::RenderPacket &left,
                                          const ::Engine::Renderer::RenderPacket &right) {
        if (left.Pipeline.Get() != right.Pipeline.Get())
            return left.Pipeline.Get() < right.Pipeline.Get();
        if (left.MaterialBindings.Get() != right.MaterialBindings.Get())
            return left.MaterialBindings.Get() < right.MaterialBindings.Get();
        if (left.VertexBuffer.Get() != right.VertexBuffer.Get())
            return left.VertexBuffer.Get() < right.VertexBuffer.Get();
        return left.IndexBuffer.Get() < right.IndexBuffer.Get();
    };

    std::stable_sort(view->Queues.Opaque.begin(), view->Queues.Opaque.end(), lessBySubmissionState);
    std::stable_sort(view->Queues.Masked.begin(), view->Queues.Masked.end(), lessBySubmissionState);
    std::stable_sort(view->Queues.Transparent.begin(), view->Queues.Transparent.end(),
                     [](const ::Engine::Renderer::RenderPacket &left, const ::Engine::Renderer::RenderPacket &right) {
                         return left.CameraDepth > right.CameraDepth;
                     });
}

// --- View Building ---

void Engine::Scene::Engine::Renderer::BuildViews(::Engine::Renderer::FrameBlackboard &blackboard,
                                       nvrhi::ICommandList *commandList, ::Engine::Renderer::FrameContext &frameContext,
                                       ::Engine::Renderer::FrameDiagnostics &diagnostics)
{
    if (!commandList)
    {
        return;
    }

    m_SubmissionStats = {};
    m_PipelineCache.BeginFrame();
    if (!EnsurePerObjectResources(EstimateDrawCount(blackboard), frameContext, diagnostics))
    {
        ENGINE_LOG_ERROR("Render", "Per-object frame resources unavailable | estimated_draw_count: {} | frame_index: {}",
                         EstimateDrawCount(blackboard), frameContext.GetFrameIndex());
        return;
    }

    const auto &shadowViews = blackboard.GetShadowViews();
    for (uint32_t viewIndex = 0; viewIndex < shadowViews.size(); ++viewIndex)
    {
        ::Engine::Renderer::RenderView *view = shadowViews[viewIndex];
        if (view)
        {
            view->Clear();
            CullAndFillView(view);
            SortViewQueues(view);
        }
    }

    const auto &sceneViews = blackboard.GetSceneViews();
    for (uint32_t viewIndex = 0; viewIndex < sceneViews.size(); ++viewIndex)
    {
        ::Engine::Renderer::RenderView *view = sceneViews[viewIndex];
        if (view)
        {
            view->Clear();
            view->CameraBuffer = frameContext.GetOrCreateUploadBuffer(
                "ViewCameraPosition/" + std::to_string(viewIndex), sizeof(::Engine::Renderer::CameraGpuData),
                static_cast<uint32_t>(::Engine::Renderer::FrameContext::FramesInFlight + 1));
            if (view->CameraBuffer)
            {
                const ::Engine::Renderer::CameraGpuData cameraData{
                    {view->CameraPosition.x, view->CameraPosition.y, view->CameraPosition.z, 1.0f}};
                commandList->writeBuffer(view->CameraBuffer, &cameraData, sizeof(cameraData));
                diagnostics.UploadBytes += sizeof(cameraData);
            }
            CullAndFillView(view);
            SortViewQueues(view);
            BuildDebugGeometry(view);
        }
    }
    const PipelineCache::FrameStats &pipelineStats = m_PipelineCache.GetFrameStats();
    m_SubmissionStats.PipelineCacheHits = pipelineStats.Hits;
    m_SubmissionStats.PipelineCacheMisses = pipelineStats.Misses;
    m_SubmissionStats.PipelineCreations = pipelineStats.Creations;
}

void Engine::Scene::Engine::Renderer::BuildDebugGeometry(::Engine::Renderer::RenderView *view)
{
    if (!view || view->ViewType != ::Engine::Renderer::RenderView::Type::Main || !view->TargetScene)
    {
        return;
    }

    ::Engine::Renderer::DebugGeometryBuilder::BuildScene(*view->TargetScene, view->DebugSettings,
                                               view->DebugGeometryData);
}

void Engine::Scene::Engine::Renderer::BuildShadowData(Engine::Scene *scene,
                                            const std::vector<::Engine::Renderer::RenderView *> &shadowViews,
                                            uint32_t firstShadowSlot, uint32_t maxShadowedLights)
{
    if (!scene)
        return;

    uint32_t viewIdx = firstShadowSlot;

    for (uint32_t idx = 0; idx < scene->GetLights().size(); ++idx)
    {
        if (viewIdx >= firstShadowSlot + maxShadowedLights || viewIdx >= shadowViews.size())
            break;

        auto lightData = scene->GetLights()[idx]->GetDirectionalLightData();
        const bool usePosition = lightData.ColorUsePosition.w > 0.5f;
        const bool castShadows = lightData.ShadowParams.w > 0.5f;
        if (!usePosition || !castShadows)
            continue;

        const DirectX::XMMATRIX lightViewProj = ComputeSpotLightViewProj(lightData);

        ::Engine::Renderer::RenderView *currentShadowView = shadowViews[viewIdx];
        currentShadowView->TargetScene = scene;
        currentShadowView->ViewProjection = lightViewProj;
        currentShadowView->ExtractFrustumPlanes();

        viewIdx++;
    }
}

uint32_t Engine::Scene::Engine::Renderer::CountShadowCasters(const Engine::Scene *scene, uint32_t limit) const
{
    if (!scene || limit == 0)
        return 0;

    uint32_t count = 0;
    for (const auto &light : scene->GetLights())
    {
        if (!light || count == limit || count == MaxShadowedLights)
            break;
        const ::Engine::Renderer::DirectionalLightData lightData = light->GetDirectionalLightData();
        if (lightData.ColorUsePosition.w > 0.5f && lightData.ShadowParams.w > 0.5f)
        {
            ++count;
        }
    }
    return count;
}

void Engine::Scene::Engine::Renderer::BuildLighting(Engine::Scene *scene,
                                          const std::vector<::Engine::Renderer::RenderView *> &shadowViews,
                                          std::vector<::Engine::Renderer::DirectionalLightData> &lights)
{
    if (!scene)
        return;

    lights.reserve(scene->GetLights().size());

    uint32_t shadowSearchStart = 0;

    for (const auto &light : scene->GetLights())
    {
        if (lights.size() >= MaxLights)
        {
            break;
        }

        auto lightData = light->GetDirectionalLightData();

        const bool usePosition = lightData.ColorUsePosition.w > 0.5f;
        const bool castShadows = lightData.ShadowParams.w > 0.5f;

        if (usePosition && castShadows)
        {
            for (uint32_t shadowIdx = shadowSearchStart; shadowIdx < shadowViews.size(); ++shadowIdx)
            {
                const ::Engine::Renderer::RenderView *shadowView = shadowViews[shadowIdx];
                if (!shadowView || shadowView->TargetScene != scene)
                    continue;
                DirectX::XMStoreFloat4x4(&lightData.LightViewProj,
                                         DirectX::XMMatrixTranspose(shadowView->ViewProjection));
                lightData.ShadowParams.x = static_cast<float>(shadowIdx);
                shadowSearchStart = shadowIdx + 1;
                break;
            }
        }

        lights.push_back(lightData);
    }
}

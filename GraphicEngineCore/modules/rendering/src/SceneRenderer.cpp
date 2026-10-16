#include "rendering/src/SceneRenderer.h"
#include "runtime/include/LogMacros.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

#include "rendering/include/renderer/DebugGeometry.h"
#include "rendering/include/renderer/Material.h"
#include "rendering/include/renderer/Mesh.h"
#include "rendering/src/MaterialBackend.h"
#include "rendering/src/MeshBackend.h"
#include "scene/include/Scene.h"

namespace {
struct DebugViewData {
  DirectX::XMFLOAT4X4 ViewProjection;
};

auto Equal(const DirectX::XMFLOAT3& left, const DirectX::XMFLOAT3& right) noexcept -> bool {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access) // DirectXMath exposes matrix elements through its documented
// union layout.
auto Equal(const DirectX::XMFLOAT4X4& left, const DirectX::XMFLOAT4X4& right) noexcept -> bool {
  return left._11 == right._11 && left._12 == right._12 && left._13 == right._13 && left._14 == right._14 &&
         left._21 == right._21 && left._22 == right._22 && left._23 == right._23 && left._24 == right._24 &&
         left._31 == right._31 && left._32 == right._32 && left._33 == right._33 && left._34 == right._34 &&
         left._41 == right._41 && left._42 == right._42 && left._43 == right._43 && left._44 == right._44;
}
// NOLINTEND(cppcoreguidelines-pro-type-union-access)

auto NormalizeOrDefault(const DirectX::XMFLOAT3& value) -> DirectX::XMVECTOR {
  DirectX::XMVECTOR vector = DirectX::XMLoadFloat3(&value);
  const float lengthSquared = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(vector));
  if (!std::isfinite(lengthSquared) || lengthSquared < 1e-6F) {
    return DirectX::XMVectorSet(0.0F, -1.0F, 0.0F, 0.0F);
  }
  return DirectX::XMVector3Normalize(vector);
}

auto MaterialPropertiesFromDocument(const ::Engine::SceneMaterialDocument& document)
    -> ::Engine::Renderer::MaterialProperties {
  ::Engine::Renderer::MaterialProperties properties;
  properties.BaseColor = {document.baseColor[0], document.baseColor[1], document.baseColor[2], document.baseColor[3]};
  properties.Emissive = {document.emissive[0], document.emissive[1], document.emissive[2]};
  properties.Metallic = document.metallic;
  properties.Roughness = document.roughness;
  properties.GtrGamma = document.gtrGamma;
  properties.Ior = document.ior;
  properties.SpecularF0 = {document.specularF0[0], document.specularF0[1], document.specularF0[2]};
  properties.Model = document.brdfModel == "ggx"   ? ::Engine::Renderer::BrdfModel::GGX
                     : document.brdfModel == "gtr" ? ::Engine::Renderer::BrdfModel::GTR
                                                   : ::Engine::Renderer::BrdfModel::Legacy;
  properties.Workflow = document.workflow == "specularGlossiness"
                            ? ::Engine::Renderer::MaterialWorkflow::SpecularGlossiness
                            : ::Engine::Renderer::MaterialWorkflow::MetallicRoughness;
  properties.SpecularMode = document.specularMode == "explicitF0"
                                ? ::Engine::Renderer::DielectricSpecularMode::ExplicitF0
                                : ::Engine::Renderer::DielectricSpecularMode::IndexOfRefraction;
  properties.AlphaMode = document.alphaMode == "mask"    ? ::Engine::Renderer::MaterialAlphaMode::Mask
                         : document.alphaMode == "blend" ? ::Engine::Renderer::MaterialAlphaMode::Blend
                                                         : ::Engine::Renderer::MaterialAlphaMode::Opaque;
  properties.AlphaCutoff = document.alphaCutoff;
  properties.DoubleSided = document.doubleSided;
  return properties;
}

static_assert(sizeof(::Engine::Renderer::PerObjectGpuData) == Engine::SceneRenderer::PerObjectDataStride);

auto ComputeSpotLightViewProj(const ::Engine::Renderer::DirectionalLightData& lightData) -> DirectX::XMMATRIX {
  using namespace DirectX;

  XMFLOAT3 position = XMFLOAT3(lightData.PositionRange.x, lightData.PositionRange.y, lightData.PositionRange.z);
  XMFLOAT3 direction =
      XMFLOAT3(lightData.DirectionIntensity.x, lightData.DirectionIntensity.y, lightData.DirectionIntensity.z);

  XMVECTOR dir = XMVector3Normalize(XMLoadFloat3(&direction));
  if (XMVectorGetX(XMVector3LengthSq(dir)) < 1e-6F) {
    dir = XMVectorSet(0.0F, -1.0F, 0.0F, 0.0F);
  }

  XMVECTOR up = XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F);
  const float upDot = std::abs(XMVectorGetX(XMVector3Dot(up, dir)));
  if (upDot > 0.98F) {
    up = XMVectorSet(1.0F, 0.0F, 0.0F, 0.0F);
  }

  XMVECTOR pos = XMLoadFloat3(&position);
  XMMATRIX view = XMMatrixLookToLH(pos, dir, up);

  const float outerCos = std::clamp(lightData.ConeParams.y, -1.0F, 1.0F);
  const float outerAngle = std::acos(outerCos);
  const float fovY = std::clamp(outerAngle * 2.0F, 0.2F, DirectX::XM_PI - 0.01F);
  const float nearPlane = 0.05F;
  const float farPlane = std::max(lightData.PositionRange.w, nearPlane + 0.01F);

  XMMATRIX proj = XMMatrixPerspectiveFovLH(fovY, 1.0F, nearPlane, farPlane);
  return view * proj;
}

auto ComputeDirectionalLightViewProj(const ::Engine::Renderer::DirectionalLightData& lightData) -> DirectX::XMMATRIX {
  using namespace DirectX;
  const XMVECTOR direction = NormalizeOrDefault(
      XMFLOAT3(lightData.DirectionIntensity.x, lightData.DirectionIntensity.y, lightData.DirectionIntensity.z));
  const XMVECTOR target = XMVectorZero();
  const XMVECTOR eye = XMVectorSubtract(target, XMVectorScale(direction, 100.0F));
  XMVECTOR up = XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F);
  if (std::abs(XMVectorGetX(XMVector3Dot(up, direction))) > 0.98F) {
    up = XMVectorSet(1.0F, 0.0F, 0.0F, 0.0F);
  }
  return XMMatrixLookAtLH(eye, target, up) * XMMatrixOrthographicLH(200.0F, 200.0F, 0.1F, 300.0F);
}

auto ComputePointLightFaceViewProj(const ::Engine::Renderer::DirectionalLightData& lightData, uint32_t face)
    -> DirectX::XMMATRIX {
  using namespace DirectX;
  constexpr XMFLOAT3 directions[] = {{1.0F, 0.0F, 0.0F},  {-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F},
                                     {0.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},  {0.0F, 0.0F, -1.0F}};
  constexpr XMFLOAT3 ups[] = {{0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, -1.0F},
                              {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
  const XMVECTOR position =
      XMVectorSet(lightData.PositionRange.x, lightData.PositionRange.y, lightData.PositionRange.z, 1.0F);
  const XMVECTOR direction = XMLoadFloat3(&directions[face]);
  const XMVECTOR up = XMLoadFloat3(&ups[face]);
  const XMMATRIX view = XMMatrixLookToLH(position, direction, up);
  const float farPlane = std::max(lightData.PositionRange.w, 0.06F);
  return view * XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.0F, 0.05F, farPlane);
}

auto ToDirectionalLightData(const ::Engine::Ecs::SpotLight& light, const ::Engine::Ecs::WorldTransform& transform)
    -> ::Engine::Renderer::DirectionalLightData {
  ::Engine::Renderer::DirectionalLightData data;
  const float innerRadians = DirectX::XMConvertToRadians(light.InnerAngleDegrees);
  const float outerRadians = DirectX::XMConvertToRadians(light.OuterAngleDegrees);
  const DirectX::XMVECTOR localDirection =
      DirectX::XMVectorSet(light.Direction[0], light.Direction[1], light.Direction[2], 0.0F);
  const DirectX::XMVECTOR worldDirection = DirectX::XMVector3Normalize(
      DirectX::XMVector3TransformNormal(localDirection, DirectX::XMLoadFloat4x4(&transform.Matrix)));
  DirectX::XMFLOAT3 direction{};
  DirectX::XMStoreFloat3(&direction, worldDirection);
  data.DirectionIntensity = {direction.x, direction.y, direction.z, light.Intensity};
  const bool positional = light.Type != ::Engine::Ecs::LightType::Directional;
  const bool point = light.Type == ::Engine::Ecs::LightType::Point;
  data.ColorUsePosition = {light.Color[0], light.Color[1], light.Color[2], positional ? 1.0F : 0.0F};
  DirectX::XMFLOAT3 position{};
  DirectX::XMStoreFloat3(
      &position, DirectX::XMVector3TransformCoord(DirectX::XMVectorZero(), DirectX::XMLoadFloat4x4(&transform.Matrix)));
  data.PositionRange = {position.x, position.y, position.z, light.Range};
  data.ConeParams = {std::cos(innerRadians), std::cos(outerRadians), point ? 1.0F : 0.0F, 0.0F};
  data.ShadowParams = {-1.0F, light.ShadowBias, light.ShadowNormalBias, light.CastShadows ? 1.0F : 0.0F};
  return data;
}

auto PositiveQuadraticRoot(float quadraticA, float quadraticB, float quadraticC) -> std::optional<float> {
  if (std::abs(quadraticA) < 1e-6F) {
    if (std::abs(quadraticB) > 1e-6F) {
      const float root = -quadraticC / quadraticB;
      return root > 0.0F ? std::optional<float>(root) : std::nullopt;
    }
    return std::nullopt;
  }

  const float discriminant = (quadraticB * quadraticB) - (4.0F * quadraticA * quadraticC);
  if (discriminant < 0.0F) {
    return std::nullopt;
  }
  const float sqrtDiscriminant = std::sqrt(discriminant);
  const float denominator = 2.0F * quadraticA;
  const float rootA = (-quadraticB - sqrtDiscriminant) / denominator;
  const float rootB = (-quadraticB + sqrtDiscriminant) / denominator;
  if (rootA > 0.0F && rootB > 0.0F) {
    return (std::min)(rootA, rootB);
  }
  if (rootA > 0.0F) {
    return rootA;
  }
  return rootB > 0.0F ? std::optional<float>(rootB) : std::nullopt;
}

auto FindLightTargetDistance(DirectX::FXMVECTOR rayOrigin, DirectX::FXMVECTOR rayDirection,
                             const ::Engine::Renderer::DebugLightTarget& target) -> std::optional<float> {
  const DirectX::XMVECTOR axis = NormalizeOrDefault(target.Direction);
  const DirectX::XMVECTOR offset = DirectX::XMVectorSubtract(rayOrigin, DirectX::XMLoadFloat3(&target.Position));
  const float axisRayDot = DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, rayDirection));
  const float axisOffsetDot = DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, offset));
  const float safeRange = (std::max)(target.Range, 0.25F);
  const float coneSlope = std::tan(std::clamp(target.OuterAngleRadians, 0.0F, DirectX::XM_PIDIV2 - 0.001F));
  const float coneScale = 1.0F + (coneSlope * coneSlope);
  const float quadraticA = 1.0F - (axisRayDot * axisRayDot * coneScale);
  const float quadraticB = 2.0F * (DirectX::XMVectorGetX(DirectX::XMVector3Dot(offset, rayDirection)) -
                                   (axisOffsetDot * axisRayDot * coneScale));
  const float quadraticC =
      DirectX::XMVectorGetX(DirectX::XMVector3Dot(offset, offset)) - (axisOffsetDot * axisOffsetDot * coneScale);
  std::optional<float> hitDistance = PositiveQuadraticRoot(quadraticA, quadraticB, quadraticC);
  if (!hitDistance) {
    const float sphereRadius = (std::max)(0.2F, safeRange * 0.025F);
    const float sphereA = DirectX::XMVectorGetX(DirectX::XMVector3Dot(rayDirection, rayDirection));
    const float sphereB = 2.0F * DirectX::XMVectorGetX(DirectX::XMVector3Dot(offset, rayDirection));
    const float sphereC = DirectX::XMVectorGetX(DirectX::XMVector3Dot(offset, offset)) - (sphereRadius * sphereRadius);
    hitDistance = PositiveQuadraticRoot(sphereA, sphereB, sphereC);
  }
  if (!hitDistance) {
    return std::nullopt;
  }

  const DirectX::XMVECTOR hitPoint =
      DirectX::XMVectorAdd(rayOrigin, DirectX::XMVectorScale(rayDirection, *hitDistance));
  const float hitAxialDistance = DirectX::XMVectorGetX(
      DirectX::XMVector3Dot(DirectX::XMVectorSubtract(hitPoint, DirectX::XMLoadFloat3(&target.Position)), axis));
  if (hitAxialDistance < -0.25F || hitAxialDistance > safeRange + 0.25F) {
    return std::nullopt;
  }
  return hitDistance;
}
} // namespace

// --- Initialization ---

Engine::SceneRenderer::SceneRenderer(nvrhi::IDevice* /*unused*/) {}

void Engine::SceneRenderer::Init(nvrhi::IDevice* device, ::Engine::Renderer::MaterialSystem* materialSystem,
                                 ::Engine::Renderer::ShaderFactory* shaderFactory,
                                 const ::Engine::Renderer::ModelResourceLibrary* modelResources) {
  m_MaterialSystem = materialSystem;
  m_ModelResources = modelResources;
  m_PipelineCache.Init(device);

  m_Device = device;

  if (shaderFactory != nullptr) {
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
      nvrhi::BindingLayoutItem::ConstantBuffer(2),
      nvrhi::BindingLayoutItem::Texture_SRV(6),
      nvrhi::BindingLayoutItem::Sampler(6),
      nvrhi::BindingLayoutItem::ConstantBuffer(4),
  };
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
      .setBorderColor(nvrhi::Color(1.0F, 1.0F, 1.0F, 1.0F));
  m_ShadowSampler = device->createSampler(shadowSamplerDesc);
}

void Engine::SceneRenderer::SetAssetTextureResolver(
    std::function<std::shared_ptr<::Engine::Renderer::TextureResource>(const std::string&, bool)> resolver) {
  m_AssetTextureResolver = std::move(resolver);
}

void Engine::SceneRenderer::EnsureDebugBuffer(nvrhi::BufferHandle& buffer, uint64_t& capacity, size_t vertexCount,
                                              const char* debugName) {
  if (vertexCount == 0) {
    return;
  }

  const uint64_t requiredBytes = static_cast<uint64_t>(vertexCount) * sizeof(::Engine::Renderer::DebugVertex);
  if ((buffer != nullptr) && capacity >= requiredBytes) {
    return;
  }

  uint64_t newCapacity =
      (std::max)(requiredBytes, static_cast<uint64_t>(sizeof(::Engine::Renderer::DebugVertex) * 256));
  while (newCapacity < requiredBytes && newCapacity <= UINT64_MAX / 2) {
    newCapacity *= 2;
  }

  nvrhi::BufferDesc desc;
  desc.byteSize = newCapacity;
  desc.isVertexBuffer = true;
  desc.initialState = nvrhi::ResourceStates::VertexBuffer;
  desc.keepInitialState = true;
  desc.debugName = (debugName != nullptr) ? debugName : "DebugGeometry_Buffer";
  nvrhi::BufferHandle newBuffer = (m_Device != nullptr) ? m_Device->createBuffer(desc) : nullptr;
  if (newBuffer != nullptr) {
    buffer = std::move(newBuffer);
    capacity = newCapacity;
  }
}

auto Engine::SceneRenderer::GetDebugPipeline(const nvrhi::FramebufferInfo& framebuffer,
                                             nvrhi::PrimitiveType primitiveType,
                                             ::Engine::Renderer::DebugDepthMode depthMode)
    -> nvrhi::GraphicsPipelineHandle {
  if ((m_DebugVS == nullptr) || (m_DebugPS == nullptr) || (m_DebugBindingLayout == nullptr)) {
    return nullptr;
  }

  Engine::PipelineKey key;
  key.VS = m_DebugVS;
  key.PS = m_DebugPS;
  key.PerObjectLayout = m_DebugBindingLayout;
  key.PrimType = primitiveType;
  key.InputLayout = {
      nvrhi::VertexAttributeDesc()
          .setName("POSITION")
          .setFormat(nvrhi::Format::RGB32_FLOAT)
          .setOffset(offsetof(::Engine::Renderer::DebugVertex, Position))
          .setElementStride(sizeof(::Engine::Renderer::DebugVertex)),
      nvrhi::VertexAttributeDesc()
          .setName("COLOR")
          .setFormat(nvrhi::Format::RGBA32_FLOAT)
          .setOffset(offsetof(::Engine::Renderer::DebugVertex, Color))
          .setElementStride(sizeof(::Engine::Renderer::DebugVertex)),
  };
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

void Engine::SceneRenderer::RenderDebugGeometry(nvrhi::ICommandList* commandList,
                                                const ::Engine::Renderer::RenderView& view) {
  if ((commandList == nullptr) || (view.TargetFramebuffer == nullptr) || view.DebugGeometryData.Empty() ||
      (m_DebugViewBuffer == nullptr) || (m_DebugBindingLayout == nullptr)) {
    return;
  }

  const ::Engine::Renderer::DebugGeometry& geometry = view.DebugGeometryData;
  EnsureDebugBuffer(m_DebugLineBuffer, m_DebugLineBufferCapacity, geometry.GetLineVertices().size(),
                    "DebugLineVertices");
  EnsureDebugBuffer(m_DebugTriangleBuffer, m_DebugTriangleBufferCapacity, geometry.GetTriangleVertices().size(),
                    "DebugTriangleVertices");

  if ((!geometry.GetLineVertices().empty() && (m_DebugLineBuffer == nullptr)) ||
      (!geometry.GetTriangleVertices().empty() && (m_DebugTriangleBuffer == nullptr))) {
    return;
  }

  DebugViewData viewData{};
  DirectX::XMStoreFloat4x4(&viewData.ViewProjection, DirectX::XMMatrixTranspose(view.ViewProjection));
  commandList->writeBuffer(m_DebugViewBuffer, &viewData, sizeof(viewData));

  nvrhi::BindingSetDesc bindingDesc;
  bindingDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_DebugViewBuffer));
  nvrhi::BindingSetHandle bindings = m_Device->createBindingSet(bindingDesc, m_DebugBindingLayout);
  if (bindings == nullptr) {
    return;
  }

  const auto framebufferInfo = view.TargetFramebuffer->getFramebufferInfo();
  const nvrhi::Viewport viewport =
      nvrhi::Viewport(static_cast<float>(view.ViewportWidth), static_cast<float>(view.ViewportHeight));
  const nvrhi::Rect scissor = nvrhi::Rect(static_cast<int>(view.ViewportWidth), static_cast<int>(view.ViewportHeight));

  const auto drawVertices = [&](const std::vector<::Engine::Renderer::DebugVertex>& vertices,
                                const nvrhi::BufferHandle& vertexBuffer, nvrhi::PrimitiveType primitiveType) -> void {
    if (vertices.empty() || !vertexBuffer) {
      return;
    }

    commandList->writeBuffer(vertexBuffer, vertices.data(), vertices.size() * sizeof(::Engine::Renderer::DebugVertex));
    nvrhi::GraphicsPipelineHandle pipeline =
        GetDebugPipeline(framebufferInfo, primitiveType, view.DebugSettings.DepthMode);
    if (!pipeline) {
      return;
    }

    nvrhi::GraphicsState state;
    state.pipeline = pipeline;
    state.framebuffer = view.TargetFramebuffer;
    state.viewport.addViewport(viewport);
    state.viewport.addScissorRect(scissor);
    state.bindings = {bindings};
    state.vertexBuffers = {nvrhi::VertexBufferBinding{.buffer = vertexBuffer, .slot = 0, .offset = 0}};
    commandList->setGraphicsState(state);

    nvrhi::DrawArguments arguments;
    arguments.vertexCount = static_cast<uint32_t>(vertices.size());
    arguments.instanceCount = 1;
    commandList->draw(arguments);
  };

  drawVertices(geometry.GetTriangleVertices(), m_DebugTriangleBuffer, nvrhi::PrimitiveType::TriangleList);
  drawVertices(geometry.GetLineVertices(), m_DebugLineBuffer, nvrhi::PrimitiveType::LineList);
}

auto Engine::SceneRenderer::PickLight(const ::Engine::Renderer::RenderView& view, float viewportX, float viewportY)
    -> std::optional<Engine::LightId> {
  if (view.ViewportWidth == 0 || view.ViewportHeight == 0 || viewportX < 0.0F || viewportY < 0.0F ||
      viewportX >= static_cast<float>(view.ViewportWidth) || viewportY >= static_cast<float>(view.ViewportHeight)) {
    return std::nullopt;
  }

  const float ndcX = (viewportX / static_cast<float>(view.ViewportWidth) * 2.0F) - 1.0F;
  const float ndcY = 1.0F - (viewportY / static_cast<float>(view.ViewportHeight) * 2.0F);
  const DirectX::XMMATRIX inverseViewProjection = DirectX::XMMatrixInverse(nullptr, view.ViewProjection);
  const DirectX::XMVECTOR clipNear = DirectX::XMVectorSet(ndcX, ndcY, 0.0F, 1.0F);
  const DirectX::XMVECTOR clipFar = DirectX::XMVectorSet(ndcX, ndcY, 1.0F, 1.0F);
  DirectX::XMVECTOR rayOrigin = DirectX::XMVector4Transform(clipNear, inverseViewProjection);
  DirectX::XMVECTOR rayEnd = DirectX::XMVector4Transform(clipFar, inverseViewProjection);
  const float originW = DirectX::XMVectorGetW(rayOrigin);
  const float endW = DirectX::XMVectorGetW(rayEnd);
  if (!std::isfinite(originW) || !std::isfinite(endW) || std::abs(originW) < 1e-5F || std::abs(endW) < 1e-5F) {
    return std::nullopt;
  }

  rayOrigin = DirectX::XMVectorScale(rayOrigin, 1.0F / originW);
  rayEnd = DirectX::XMVectorScale(rayEnd, 1.0F / endW);
  const DirectX::XMVECTOR rayDirection = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(rayEnd, rayOrigin));
  const auto& targets = view.DebugGeometryData.GetLightTargets();
  float closestDistance = std::numeric_limits<float>::max();
  Engine::LightId closestLight = Engine::InvalidLightId;

  for (const ::Engine::Renderer::DebugLightTarget& target : targets) {
    const std::optional<float> hitDistance = FindLightTargetDistance(rayOrigin, rayDirection, target);
    if (hitDistance && *hitDistance < closestDistance) {
      closestDistance = *hitDistance;
      closestLight = target.Id;
    }
  }

  return closestLight == Engine::InvalidLightId ? std::nullopt : std::optional<Engine::LightId>(closestLight);
}

// --- Uploads ---

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto Engine::SceneRenderer::EstimateDrawCount(const ::Engine::Renderer::FrameBlackboard& blackboard) const -> size_t {
  size_t count = 0;
  const auto accumulate = [this, &count](const std::vector<::Engine::Renderer::RenderView*>& views) -> void {
    for (const ::Engine::Renderer::RenderView* view : views) {
      if (!view || !view->TargetScene) {
        continue;
      }
      view->TargetScene->World().ForEachChunkReadOnly<::Engine::Ecs::MeshRenderer>(
          [this, &count](std::span<const Engine::EntityHandle>,
                         std::span<const ::Engine::Ecs::MeshRenderer> meshRenderers) -> void {
            for (const auto& meshRenderer : meshRenderers) {
              std::string resourceKey = meshRenderer.ModelGuid;
              if (resourceKey.empty() && !meshRenderer.PrimitiveType.empty()) {
                resourceKey = "primitive:" + meshRenderer.PrimitiveType;
              }
              const auto* submeshes = m_ModelResources == nullptr ? nullptr : m_ModelResources->Find(resourceKey);
              const size_t submeshCount = submeshes == nullptr ? 1 : submeshes->size();
              if (submeshCount > std::numeric_limits<size_t>::max() - count) {
                count = std::numeric_limits<size_t>::max();
                return;
              }
              count += submeshCount;
            }
          });
    }
  };
  accumulate(blackboard.GetShadowViews());
  accumulate(blackboard.GetSceneViews());
  return count;
}

auto Engine::SceneRenderer::EnsureLightBuffers(size_t viewCount, ::Engine::Renderer::FrameContext& frameContext)
    -> bool {
  for (size_t viewIndex = 0; viewIndex < viewCount; ++viewIndex) {
    if (frameContext.GetOrCreateUploadBuffer(
            "PerViewLights/" + std::to_string(viewIndex), sizeof(::Engine::Renderer::LightsCBData),
            static_cast<uint32_t>(::Engine::Renderer::FrameContext::FramesInFlight + 1)) == nullptr) {
      return false;
    }
  }
  return true;
}

auto Engine::SceneRenderer::GetLightBuffer(size_t viewIndex, ::Engine::Renderer::FrameContext& frameContext)
    -> nvrhi::BufferHandle {
  return frameContext.GetOrCreateUploadBuffer(
      "PerViewLights/" + std::to_string(viewIndex), sizeof(::Engine::Renderer::LightsCBData),
      static_cast<uint32_t>(::Engine::Renderer::FrameContext::FramesInFlight + 1));
}

auto Engine::SceneRenderer::GetSceneBindingSet(size_t viewIndex, nvrhi::BufferHandle lightBuffer,
                                               nvrhi::TextureHandle shadowMap, nvrhi::SamplerHandle shadowSampler,
                                               nvrhi::BufferHandle cameraBuffer,
                                               ::Engine::Renderer::FrameDiagnostics& diagnostics)
    -> nvrhi::BindingSetHandle {
  if ((m_Device == nullptr) || (lightBuffer == nullptr) || (shadowMap == nullptr) || (shadowSampler == nullptr) ||
      (cameraBuffer == nullptr) || (m_SceneBindingLayout == nullptr)) {
    return nullptr;
  }
  if (m_SceneBindingCaches.size() <= viewIndex) {
    m_SceneBindingCaches.resize(viewIndex + 1);
  }
  auto& cache = m_SceneBindingCaches.at(viewIndex);
  if ((cache.Bindings != nullptr) && cache.LightBuffer == lightBuffer && cache.ShadowMap == shadowMap &&
      cache.ShadowSampler == shadowSampler && cache.CameraBuffer == cameraBuffer &&
      cache.Layout == m_SceneBindingLayout) {
    return cache.Bindings;
  }

  nvrhi::BindingSetDesc desc;
  desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(2, lightBuffer));
  desc.addItem(nvrhi::BindingSetItem::Texture_SRV(6, shadowMap));
  desc.addItem(nvrhi::BindingSetItem::Sampler(6, shadowSampler));
  desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(4, cameraBuffer));
  nvrhi::BindingSetHandle bindings = m_Device->createBindingSet(desc, m_SceneBindingLayout);
  if (bindings == nullptr) {
    return nullptr;
  }
  cache.LightBuffer = std::move(lightBuffer);
  cache.ShadowMap = std::move(shadowMap);
  cache.ShadowSampler = std::move(shadowSampler);
  cache.CameraBuffer = std::move(cameraBuffer);
  cache.Layout = m_SceneBindingLayout;
  cache.Bindings = std::move(bindings);
  ++diagnostics.BindingSetAllocations;
  return cache.Bindings;
}

auto Engine::SceneRenderer::EnsurePerObjectResources(size_t maxDrawCount,
                                                     ::Engine::Renderer::FrameContext& frameContext,
                                                     ::Engine::Renderer::FrameDiagnostics& diagnostics) -> bool {
  maxDrawCount = (std::max)(maxDrawCount, size_t{1});
  if (maxDrawCount <= m_PerObjectBufferCapacity && (m_PerObjectBuffer != nullptr) &&
      (m_PerObjectBindingSet != nullptr)) {
    return true;
  }
  if (maxDrawCount > std::numeric_limits<uint32_t>::max()) {
    return false;
  }

  nvrhi::BufferHandle buffer = frameContext.GetOrCreateUploadBuffer(
      "PerObjectDataRing", static_cast<uint32_t>(PerObjectDataStride), static_cast<uint32_t>(maxDrawCount));
  if (buffer == nullptr) {
    return false;
  }

  nvrhi::BindingSetDesc setDesc;
  setDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, buffer));
  nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(setDesc, m_PerObjectBindingLayout);
  if (bindingSet == nullptr) {
    return false;
  }

  m_PerObjectBuffer = std::move(buffer);
  m_PerObjectBindingSet = std::move(bindingSet);
  m_PerObjectBufferCapacity = maxDrawCount;
  ++diagnostics.BindingSetAllocations;
  return true;
}

auto Engine::SceneRenderer::IsSubmeshVisible(::Engine::Renderer::RenderView* view,
                                             const ::Engine::Renderer::Submesh& submesh, DirectX::FXMMATRIX world,
                                             const DirectX::XMFLOAT4X4& worldTransform) -> bool {
  const auto cullingStart = std::chrono::steady_clock::now();
  const bool boundsCacheMatches = submesh.WorldBoundsCached && Equal(worldTransform, submesh.CachedWorldTransform) &&
                                  Equal(submesh.BoundsMin, submesh.CachedBoundsMin) &&
                                  Equal(submesh.BoundsMax, submesh.CachedBoundsMax);
  if (!boundsCacheMatches) {
    const DirectX::XMFLOAT3 localCenter = {(submesh.BoundsMin.x + submesh.BoundsMax.x) * 0.5F,
                                           (submesh.BoundsMin.y + submesh.BoundsMax.y) * 0.5F,
                                           (submesh.BoundsMin.z + submesh.BoundsMax.z) * 0.5F};
    const DirectX::XMVECTOR center = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&localCenter), world);
    float radius = 0.0F;
    for (uint32_t corner = 0; corner < 8; ++corner) {
      const DirectX::XMFLOAT3 point = {(corner & 1U) != 0U ? submesh.BoundsMax.x : submesh.BoundsMin.x,
                                       (corner & 2U) != 0U ? submesh.BoundsMax.y : submesh.BoundsMin.y,
                                       (corner & 4U) != 0U ? submesh.BoundsMax.z : submesh.BoundsMin.z};
      const DirectX::XMVECTOR worldPoint = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&point), world);
      radius =
          (std::max)(radius,
                     DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(worldPoint, center))));
    }
    DirectX::XMStoreFloat3(&submesh.CachedWorldBoundsCenter, center);
    submesh.CachedWorldBoundsRadius = radius;
    submesh.CachedWorldTransform = worldTransform;
    submesh.CachedBoundsMin = submesh.BoundsMin;
    submesh.CachedBoundsMax = submesh.BoundsMax;
    submesh.WorldBoundsCached = true;
  }
  const bool visible = view->IsSphereVisible(submesh.CachedWorldBoundsCenter, submesh.CachedWorldBoundsRadius);
  m_SubmissionStats.CullingTimeMs +=
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cullingStart).count();
  return visible;
}

auto Engine::SceneRenderer::ConfigureAndQueuePacket(::Engine::Renderer::RenderView* view,
                                                    const ::Engine::Ecs::MeshRenderer& meshRenderer,
                                                    const ::Engine::Renderer::Submesh& submesh,
                                                    const ::Engine::Renderer::MaterialProperties& materialProperties,
                                                    const nvrhi::FramebufferInfoEx& framebufferInfo,
                                                    ::Engine::Renderer::RenderPacket& packet,
                                                    bool disableFaceCulling) -> bool {
  if (view->ViewType == ::Engine::Renderer::RenderView::Type::Shadow &&
      materialProperties.AlphaMode == ::Engine::Renderer::MaterialAlphaMode::Blend) {
    return false;
  }
  packet.ShadowAlphaTest = view->ViewType == ::Engine::Renderer::RenderView::Type::Shadow &&
                           materialProperties.AlphaMode == ::Engine::Renderer::MaterialAlphaMode::Mask;
  packet.Queue = ::Engine::Renderer::RenderQueueType::Opaque;
  if (view->ViewType != ::Engine::Renderer::RenderView::Type::Shadow) {
    if (materialProperties.AlphaMode == ::Engine::Renderer::MaterialAlphaMode::Mask) {
      packet.Queue = ::Engine::Renderer::RenderQueueType::Masked;
    } else if (materialProperties.AlphaMode == ::Engine::Renderer::MaterialAlphaMode::Blend) {
      packet.Queue = ::Engine::Renderer::RenderQueueType::Transparent;
    }
  }
  packet.CameraDepth = DirectX::XMVectorGetZ(
      DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&submesh.CachedWorldBoundsCenter), view->ViewMatrix));
  ++m_SubmissionStats.ObjectsSubmitted;
  m_SubmissionStats.PerObjectBytesUsed += PerObjectDataStride;
  Engine::PipelineKey key;
  switch (submesh.mesh->GetPrimitiveTopology()) {
  case ::Engine::Renderer::PrimitiveTopology::Lines:
    key.PrimType = nvrhi::PrimitiveType::LineList;
    break;
  case ::Engine::Renderer::PrimitiveTopology::LineStrip:
    key.PrimType = nvrhi::PrimitiveType::LineStrip;
    break;
  case ::Engine::Renderer::PrimitiveTopology::Points:
    key.PrimType = nvrhi::PrimitiveType::PointList;
    break;
  case ::Engine::Renderer::PrimitiveTopology::Triangles:
  default:
    key.PrimType = nvrhi::PrimitiveType::TriangleList;
    break;
  }
  key.VS = m_MaterialSystem->GetStandardVS();
  key.PerObjectLayout = m_PerObjectBindingLayout;
  key.SceneLayout = m_SceneBindingLayout;
  key.MaterialLayout = m_MaterialSystem->GetStandardMaterialLayout();
  key.DepthWrite = packet.Queue != ::Engine::Renderer::RenderQueueType::Transparent;
  if (view->ViewType == ::Engine::Renderer::RenderView::Type::Shadow) {
    key.PS = packet.ShadowAlphaTest ? m_ShadowMaskPS : nullptr;
    key.SceneLayout = nullptr;
    key.MaterialLayout = packet.ShadowAlphaTest ? m_MaterialSystem->GetStandardMaterialLayout() : nullptr;
    key.Wireframe = false;
    key.DepthTest = true;
    key.CullMode =
        (materialProperties.DoubleSided || disableFaceCulling) ? nvrhi::RasterCullMode::None
                                                              : nvrhi::RasterCullMode::Back;
  } else {
    key.PS =
        submesh.material->HasTextureOverrides() ? m_MaterialSystem->GetTexturedPS() : m_MaterialSystem->GetStandardPS();
    key.Wireframe = meshRenderer.Wireframe;
    key.DepthTest = !meshRenderer.DisableDepthTest;
    key.CullMode = (meshRenderer.Wireframe || materialProperties.DoubleSided || disableFaceCulling)
                       ? nvrhi::RasterCullMode::None
                       : nvrhi::RasterCullMode::Back;
    if (packet.Queue == ::Engine::Renderer::RenderQueueType::Transparent) {
      nvrhi::BlendState::RenderTarget blendTarget;
      blendTarget.enableBlend()
          .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
          .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
          .setSrcBlendAlpha(nvrhi::BlendFactor::One)
          .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
      key.BlendState.setRenderTarget(0, blendTarget);
      // Keep the direct and diffuse lighting contributions composited with FinalColor.
      key.BlendState.setRenderTarget(3, blendTarget);
      key.BlendState.setRenderTarget(4, blendTarget);
    }
  }
  packet.Pipeline = packet.ShadowAlphaTest && (m_ShadowMaskPS == nullptr)
                        ? nullptr
                        : m_PipelineCache.GetOrCreatePipeline(key, framebufferInfo);
  switch (packet.Queue) {
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
  return true;
}

void Engine::SceneRenderer::SubmitSubmesh(::Engine::Renderer::RenderView* view,
                                          const ::Engine::Ecs::WorldTransform& transform, ::Engine::EntityHandle entity,
                                          const ::Engine::Ecs::MeshRenderer& meshRenderer,
                                          const ::Engine::Renderer::Submesh& submesh, DirectX::FXMMATRIX viewProjection,
                                          const nvrhi::FramebufferInfoEx& framebufferInfo) {
  if (!submesh.mesh || !submesh.material) {
    ++m_SubmissionStats.ObjectsCulled;
    return;
  }
  const DirectX::XMMATRIX entityWorld = DirectX::XMLoadFloat4x4(&transform.Matrix);
  const DirectX::XMMATRIX world = DirectX::XMLoadFloat4x4(&submesh.LocalTransform) * entityWorld;
  DirectX::XMFLOAT4X4 worldTransform{};
  DirectX::XMStoreFloat4x4(&worldTransform, world);
  if (!IsSubmeshVisible(view, submesh, world, worldTransform)) {
    ++m_SubmissionStats.ObjectsCulled;
    return;
  }
  const auto packetBuildStart = std::chrono::steady_clock::now();
  ::Engine::Renderer::PerObjectGpuData perObjectData{};
  const DirectX::XMMATRIX mvp = world * viewProjection;
  DirectX::XMVECTOR determinant;
  const DirectX::XMMATRIX inverseWorld = DirectX::XMMatrixInverse(&determinant, world);
  const DirectX::XMMATRIX normal = std::abs(DirectX::XMVectorGetX(determinant)) > 1.0e-8F
                                       ? DirectX::XMMatrixTranspose(inverseWorld)
                                       : DirectX::XMMatrixIdentity();
  DirectX::XMStoreFloat4x4(&perObjectData.World, DirectX::XMMatrixTranspose(world));
  DirectX::XMStoreFloat4x4(&perObjectData.MVP, DirectX::XMMatrixTranspose(mvp));
  DirectX::XMStoreFloat4x4(&perObjectData.Normal, DirectX::XMMatrixTranspose(normal));
  perObjectData.ObjectParams = {meshRenderer.ReceiveShadows ? 1.0F : 0.0F, meshRenderer.Wireframe ? 1.0F : 0.0F, 0.0F,
                                0.0F};
  const auto material = MaterialForSubmesh(view, entity, meshRenderer, submesh);
  auto packet = CreateRenderPacket(submesh.mesh.get(), material.get());
  packet.PerObjectData = perObjectData;
  const bool disableFaceCulling =
      transform.HasAxisFlip || DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(world)) < 0.0F;
  if (ConfigureAndQueuePacket(view, meshRenderer, submesh, material->GetProperties(), framebufferInfo, packet,
                              disableFaceCulling)) {
    m_SubmissionStats.PacketBuildTimeMs +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - packetBuildStart).count();
  }
}

auto Engine::SceneRenderer::MaterialForSubmesh(const ::Engine::Renderer::RenderView* view,
                                               ::Engine::EntityHandle entity,
                                               const ::Engine::Ecs::MeshRenderer& meshRenderer,
                                               const ::Engine::Renderer::Submesh& submesh)
    -> std::shared_ptr<::Engine::Renderer::Material> {
  const uint32_t slot = submesh.MaterialSlot < 0 ? 0U : static_cast<uint32_t>(submesh.MaterialSlot);
  const std::string resourceKey =
      meshRenderer.ModelGuid.empty() ? "primitive:" + meshRenderer.PrimitiveType : meshRenderer.ModelGuid;
  const std::optional<::Engine::SceneMaterialDocument>* selectedOverride = nullptr;
  if (submesh.MaterialSlot >= 0 && static_cast<size_t>(submesh.MaterialSlot) < meshRenderer.MaterialOverrides.size()) {
    selectedOverride = &meshRenderer.MaterialOverrides[static_cast<size_t>(submesh.MaterialSlot)];
  }
  const ::Engine::SceneMaterialDocument* source = selectedOverride != nullptr && selectedOverride->has_value()
                                                      ? &selectedOverride->value()
                                                      : (meshRenderer.Material ? &*meshRenderer.Material : nullptr);
  const uintptr_t sceneKey = reinterpret_cast<uintptr_t>(view->TargetScene);
  const uintptr_t sourceMaterialKey = reinterpret_cast<uintptr_t>(submesh.material.get());
  const auto belongsToEntity = [sceneKey, entity](const auto& item) -> bool {
    const auto& [key, unused] = item;
    (void)unused;
    return std::get<0>(key) == sceneKey && std::get<1>(key) == entity.Index && std::get<2>(key) == entity.Generation;
  };
  const auto belongsToSlot = [&belongsToEntity, slot, sourceMaterialKey](const auto& item) -> bool {
    return belongsToEntity(item) && std::get<4>(item.first) == static_cast<int32_t>(slot) &&
           std::get<5>(item.first) == sourceMaterialKey;
  };
  std::erase_if(m_MaterialOverrideCache, [&belongsToEntity, &resourceKey](const auto& item) -> bool {
    return belongsToEntity(item) && std::get<3>(item.first) != resourceKey;
  });
  if (source == nullptr || m_MaterialSystem == nullptr) {
    std::erase_if(m_MaterialOverrideCache, belongsToSlot);
    return submesh.material;
  }

  const MaterialOverrideKey key{
      sceneKey, entity.Index, entity.Generation, resourceKey, static_cast<int32_t>(slot), sourceMaterialKey};
  std::erase_if(m_MaterialOverrideCache,
                [&belongsToSlot, &key](const auto& item) -> bool { return belongsToSlot(item) && item.first != key; });
  const auto found = m_MaterialOverrideCache.find(key);
  if (found != m_MaterialOverrideCache.end() && found->second.Source == *source) {
    return found->second.Material;
  }

  auto material = m_MaterialSystem->CreateMaterial(MaterialPropertiesFromDocument(*source));
  auto baseColorTexture = submesh.material->GetBaseColorTexture();
  auto normalTexture = submesh.material->GetNormalTexture();
  if (m_AssetTextureResolver != nullptr) {
    if (source->baseColorTextureGuid) {
      if (auto texture = m_AssetTextureResolver(*source->baseColorTextureGuid, true)) {
        baseColorTexture = std::move(texture);
      }
    }
    if (source->normalTextureGuid) {
      if (auto texture = m_AssetTextureResolver(*source->normalTextureGuid, false)) {
        normalTexture = std::move(texture);
      }
    }
  }
  if (material == nullptr ||
      !m_MaterialSystem->SetPbrTextureBindings(material, std::move(baseColorTexture), std::move(normalTexture),
                                               submesh.material->GetMetallicRoughnessTexture(),
                                               submesh.material->GetOcclusionTexture(),
                                               submesh.material->GetEmissiveTexture(), true)) {
    return submesh.material;
  }
  m_MaterialOverrideCache.insert_or_assign(key, MaterialOverrideCacheEntry{.Source = *source, .Material = material});
  return material;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void Engine::SceneRenderer::CullAndFillView(::Engine::Renderer::RenderView* view) {
  if ((view == nullptr) || (view->TargetScene == nullptr)) {
    return;
  }

  const uintptr_t sceneKey = reinterpret_cast<uintptr_t>(view->TargetScene);
  const auto& world = view->TargetScene->World();
  std::erase_if(m_MaterialOverrideCache, [&sceneKey, &world](const auto& item) -> bool {
    const auto& key = item.first;
    if (std::get<0>(key) != sceneKey) {
      return false;
    }
    return !world.IsAlive({.Index = std::get<1>(key), .Generation = std::get<2>(key)});
  });

  DirectX::XMMATRIX viewProj = view->ViewProjection;
  nvrhi::IFramebuffer* framebuffer = view->TargetFramebuffer; // FIXME

  nvrhi::FramebufferInfoEx fbInfo;
  if (framebuffer != nullptr) {
    fbInfo = framebuffer->getFramebufferInfo();
  } else if (view->ViewType == ::Engine::Renderer::RenderView::Type::Shadow) {
    fbInfo.depthFormat = nvrhi::Format::D32;
  } else {
    return;
  }

  view->TargetScene->World()
      .ForEachChunkReadOnly<::Engine::Ecs::MeshRenderer, ::Engine::Ecs::WorldTransform,
                            ::Engine::Ecs::GlobalVisibility>(
          [this, view, viewProj, &fbInfo](std::span<const Engine::EntityHandle> entities,
                                          std::span<const ::Engine::Ecs::MeshRenderer> meshRenderers,
                                          std::span<const ::Engine::Ecs::WorldTransform> transforms,
                                          std::span<const ::Engine::Ecs::GlobalVisibility> visibility) -> void {
            for (size_t row = 0; row < meshRenderers.size(); ++row) {
              const auto& meshRenderer = meshRenderers[row];
              if (!visibility[row].IsVisible || !meshRenderer.Visible ||
                  (view->ViewType == ::Engine::Renderer::RenderView::Type::Shadow && !meshRenderer.CastShadows)) {
                ++m_SubmissionStats.ObjectsCulled;
                continue;
              }
              std::string resourceKey = meshRenderer.ModelGuid;
              if (resourceKey.empty() && !meshRenderer.PrimitiveType.empty()) {
                resourceKey = "primitive:" + meshRenderer.PrimitiveType;
              }
              const auto* submeshes = m_ModelResources == nullptr ? nullptr : m_ModelResources->Find(resourceKey);
              if (submeshes == nullptr || submeshes->empty()) {
                ++m_SubmissionStats.ObjectsCulled;
                continue;
              }
              for (const auto& submesh : *submeshes) {
                SubmitSubmesh(view, transforms[row], entities[row], meshRenderer, submesh, viewProj, fbInfo);
              }
            }
          });
}

// --- RenderPacket creation ---

auto Engine::SceneRenderer::CreateRenderPacket(const ::Engine::Renderer::Mesh* mesh,
                                               const ::Engine::Renderer::Material* material)
    -> ::Engine::Renderer::RenderPacket {
  ::Engine::Renderer::RenderPacket packet;
  packet.VertexBuffer = ::Engine::Renderer::MeshBackendAccess::GetVertexBuffer(*mesh);
  packet.IndexBuffer = ::Engine::Renderer::MeshBackendAccess::GetIndexBuffer(*mesh);
  packet.VertexCount = mesh->GetVertexCount();
  packet.IndexCount = mesh->GetIndexCount();
  packet.MaterialBindings = ::Engine::Renderer::MaterialBackendAccess::GetBindingSet(*material);
  packet.BaseVertexOffset = 0;
  packet.StartIndexLocation = 0;

  return packet;
}

void Engine::SceneRenderer::SortViewQueues(::Engine::Renderer::RenderView* view) {
  if (view == nullptr) {
    return;
  }

  const auto lessBySubmissionState = [](const ::Engine::Renderer::RenderPacket& left,
                                        const ::Engine::Renderer::RenderPacket& right) -> bool {
    if (left.Pipeline.Get() != right.Pipeline.Get()) {
      return left.Pipeline.Get() < right.Pipeline.Get();
    }
    if (left.MaterialBindings.Get() != right.MaterialBindings.Get()) {
      return left.MaterialBindings.Get() < right.MaterialBindings.Get();
    }
    if (left.VertexBuffer.Get() != right.VertexBuffer.Get()) {
      return left.VertexBuffer.Get() < right.VertexBuffer.Get();
    }
    return left.IndexBuffer.Get() < right.IndexBuffer.Get();
  };

  std::ranges::stable_sort(view->Queues.Opaque, lessBySubmissionState);
  std::ranges::stable_sort(view->Queues.Masked, lessBySubmissionState);
  std::ranges::stable_sort(
      view->Queues.Transparent,
      [](const ::Engine::Renderer::RenderPacket& left, const ::Engine::Renderer::RenderPacket& right) -> bool {
        return left.CameraDepth > right.CameraDepth;
      });
}

// --- View Building ---

void Engine::SceneRenderer::BuildViews(::Engine::Renderer::FrameBlackboard& blackboard,
                                       nvrhi::ICommandList* commandList, ::Engine::Renderer::FrameContext& frameContext,
                                       ::Engine::Renderer::FrameDiagnostics& diagnostics) {
  if (commandList == nullptr) {
    return;
  }

  m_SubmissionStats = {};
  m_PipelineCache.BeginFrame();
  if (!EnsurePerObjectResources(EstimateDrawCount(blackboard), frameContext, diagnostics)) {
    ENGINE_LOG_ERROR("Render", "Per-object frame resources unavailable | estimated_draw_count: {} | frame_index: {}",
                     EstimateDrawCount(blackboard), frameContext.GetFrameIndex());
    return;
  }

  const auto& shadowViews = blackboard.GetShadowViews();
  for (auto* view : shadowViews) {
    if (view != nullptr) {
      view->Clear();
      CullAndFillView(view);
      SortViewQueues(view);
    }
  }

  const auto& sceneViews = blackboard.GetSceneViews();
  for (uint32_t viewIndex = 0; viewIndex < sceneViews.size(); ++viewIndex) {
    ::Engine::Renderer::RenderView* view = sceneViews.at(viewIndex);
    if (view != nullptr) {
      view->Clear();
      view->CameraBuffer = frameContext.GetOrCreateUploadBuffer(
          "ViewCameraPosition/" + std::to_string(viewIndex), sizeof(::Engine::Renderer::CameraGpuData),
          static_cast<uint32_t>(::Engine::Renderer::FrameContext::FramesInFlight + 1));
      if (view->CameraBuffer != nullptr) {
        const ::Engine::Renderer::CameraGpuData cameraData{
            {view->CameraPosition.x, view->CameraPosition.y, view->CameraPosition.z, 1.0F},
        };
        commandList->writeBuffer(view->CameraBuffer, &cameraData, sizeof(cameraData));
        diagnostics.UploadBytes += sizeof(cameraData);
      }
      CullAndFillView(view);
      SortViewQueues(view);
      BuildDebugGeometry(view);
    }
  }
  const PipelineCache::FrameStats& pipelineStats = m_PipelineCache.GetFrameStats();
  m_SubmissionStats.PipelineCacheHits = pipelineStats.Hits;
  m_SubmissionStats.PipelineCacheMisses = pipelineStats.Misses;
  m_SubmissionStats.PipelineCreations = pipelineStats.Creations;
}

void Engine::SceneRenderer::BuildDebugGeometry(::Engine::Renderer::RenderView* view) {
  if ((view == nullptr) || view->ViewType != ::Engine::Renderer::RenderView::Type::Main ||
      (view->TargetScene == nullptr) || view->RenderMode != Engine::ViewportBinding::Mode::Editor) {
    return;
  }

  ::Engine::Renderer::DebugGeometryBuilder::BuildScene(*view->TargetScene, view->DebugSettings,
                                                       view->DebugGeometryData);
}

void Engine::SceneRenderer::BuildShadowData(Engine::Scene* scene,
                                            const std::vector<::Engine::Renderer::RenderView*>& shadowViews,
                                            uint32_t firstShadowSlot, uint32_t maxShadowedLights) {
  if (scene == nullptr) {
    return;
  }
  uint32_t viewIndex = firstShadowSlot;
  uint32_t casterCount = 0;
  scene->World()
      .ForEachChunkReadOnly<::Engine::Ecs::SpotLight, ::Engine::Ecs::WorldTransform, ::Engine::Ecs::GlobalVisibility>(
          [scene, &shadowViews, firstShadowSlot, maxShadowedLights, &casterCount, &viewIndex](
              std::span<const Engine::EntityHandle> entities, std::span<const ::Engine::Ecs::SpotLight> lightComponents,
              std::span<const ::Engine::Ecs::WorldTransform> transforms,
              std::span<const ::Engine::Ecs::GlobalVisibility> visibility) -> void {
            for (size_t row = 0; row < lightComponents.size(); ++row) {
              if (casterCount >= maxShadowedLights || viewIndex >= shadowViews.size()) {
                return;
              }
              if (!visibility[row].IsVisible) {
                continue;
              }
              const auto lightData = ToDirectionalLightData(lightComponents[row], transforms[row]);
              if (lightData.ShadowParams.w <= 0.5F) {
                continue;
              }
              const uint32_t faceCount = lightComponents[row].Type == ::Engine::Ecs::LightType::Point ? 6U : 1U;
              ++casterCount;
              for (uint32_t face = 0; face < faceCount && viewIndex < shadowViews.size(); ++face) {
                auto* shadowView = shadowViews.at(viewIndex++);
                if (shadowView == nullptr) {
                  continue;
                }
                shadowView->TargetScene = scene;
                shadowView->ShadowLight = entities[row];
                if (lightComponents[row].Type == ::Engine::Ecs::LightType::Directional) {
                  shadowView->ViewProjection = ComputeDirectionalLightViewProj(lightData);
                } else if (faceCount == 6U) {
                  shadowView->ViewProjection = ComputePointLightFaceViewProj(lightData, face);
                } else {
                  shadowView->ViewProjection = ComputeSpotLightViewProj(lightData);
                }
                shadowView->ExtractFrustumPlanes();
              }
            }
          });
}
auto Engine::SceneRenderer::CountShadowCasters(const Engine::Scene* scene, uint32_t limit) -> uint32_t {
  if (scene == nullptr || limit == 0) {
    return 0;
  }
  uint32_t count = 0;
  scene->World().ForEachChunkReadOnly<::Engine::Ecs::SpotLight>(
      [&count, limit](std::span<const Engine::EntityHandle>,
                      std::span<const ::Engine::Ecs::SpotLight> lightComponents) -> void {
        for (const auto& light : lightComponents) {
          if (count >= limit || count >= MaxShadowedLights) {
            return;
          }
          if (light.CastShadows) {
            ++count;
          }
        }
      });
  return count;
}
auto Engine::SceneRenderer::CountShadowViews(const Engine::Scene* scene, uint32_t casterLimit) -> uint32_t {
  if (scene == nullptr || casterLimit == 0) {
    return 0;
  }
  uint32_t count = 0;
  uint32_t views = 0;
  scene->World().ForEachChunkReadOnly<::Engine::Ecs::SpotLight>(
      [&count, &views, casterLimit](std::span<const Engine::EntityHandle>,
                                    std::span<const ::Engine::Ecs::SpotLight> lights) -> void {
        for (const auto& light : lights) {
          if (count >= casterLimit || count >= MaxShadowedLights) {
            return;
          }
          if (light.CastShadows) {
            ++count;
            views += light.Type == ::Engine::Ecs::LightType::Point ? 6U : 1U;
          }
        }
      });
  return views;
}
void Engine::SceneRenderer::BuildLighting(Engine::Scene* scene,
                                          const std::vector<::Engine::Renderer::RenderView*>& shadowViews,
                                          std::vector<::Engine::Renderer::DirectionalLightData>& lights) {
  lights.clear();
  if (scene == nullptr) {
    return;
  }
  lights.reserve((std::min)(scene->EntityCount(), static_cast<size_t>(MaxLights)));
  uint32_t shadowSearchStart = 0;
  scene->World()
      .ForEachChunkReadOnly<::Engine::Ecs::SpotLight, ::Engine::Ecs::WorldTransform, ::Engine::Ecs::GlobalVisibility>(
          [&lights, &shadowSearchStart, &shadowViews, scene](
              std::span<const Engine::EntityHandle> entities, std::span<const ::Engine::Ecs::SpotLight> lightComponents,
              std::span<const ::Engine::Ecs::WorldTransform> transforms,
              std::span<const ::Engine::Ecs::GlobalVisibility> visibility) -> void {
            for (size_t row = 0; row < lightComponents.size() && lights.size() < MaxLights; ++row) {
              if (!visibility[row].IsVisible) {
                continue;
              }
              auto lightData = ToDirectionalLightData(lightComponents[row], transforms[row]);
              if (lightData.ShadowParams.w > 0.5F) {
                for (uint32_t shadowIndex = shadowSearchStart; shadowIndex < shadowViews.size(); ++shadowIndex) {
                  const auto* shadowView = shadowViews.at(shadowIndex);
                  if (shadowView == nullptr || shadowView->TargetScene != scene ||
                      shadowView->ShadowLight != entities[row]) {
                    continue;
                  }
                  lightData.ShadowParams.x = static_cast<float>(shadowIndex);
                  if (lightComponents[row].Type != ::Engine::Ecs::LightType::Point) {
                    DirectX::XMStoreFloat4x4(&lightData.LightViewProj,
                                             DirectX::XMMatrixTranspose(shadowView->ViewProjection));
                  }
                  shadowSearchStart =
                      shadowIndex + (lightComponents[row].Type == ::Engine::Ecs::LightType::Point ? 6U : 1U);
                  break;
                }
              }
              lights.push_back(lightData);
            }
          });
}

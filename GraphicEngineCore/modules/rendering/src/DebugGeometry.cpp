#include "rendering/include/renderer/DebugGeometry.h"

#include "scene/include/Scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {
constexpr float Pi = std::numbers::pi_v<float>;
constexpr float TwoPi = 2.0F * Pi;
constexpr uint32_t LightRingSegments = 32;
constexpr float WireframePlaneExtent = 10.0F;
constexpr int WireframePlaneDivisions = 20;

auto NormalizeOrDefault(const DirectX::XMFLOAT3& value) -> DirectX::XMVECTOR {
  DirectX::XMVECTOR vector = DirectX::XMLoadFloat3(&value);
  const float lengthSquared = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(vector));
  if (!std::isfinite(lengthSquared) || lengthSquared < 1e-6F) {
    return DirectX::XMVectorSet(0.0F, -1.0F, 0.0F, 0.0F);
  }
  return DirectX::XMVector3Normalize(vector);
}

auto StoreFloat3(DirectX::FXMVECTOR value) -> DirectX::XMFLOAT3 {
  DirectX::XMFLOAT3 result{};
  DirectX::XMStoreFloat3(&result, value);
  return result;
}

} // namespace

namespace Engine::Renderer {

void DebugGeometry::Clear() {
  m_LineVertices.clear();
  m_TriangleVertices.clear();
  m_LightTargets.clear();
}

void DebugGeometry::AddLine(const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& end,
                            const DirectX::XMFLOAT4& color) {
  m_LineVertices.push_back({.Position = start, .Color = color});
  m_LineVertices.push_back({.Position = end, .Color = color});
}

void DebugGeometry::AddTriangle(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, const DirectX::XMFLOAT3& c,
                                const DirectX::XMFLOAT4& color) {
  m_TriangleVertices.push_back({.Position = a, .Color = color});
  m_TriangleVertices.push_back({.Position = b, .Color = color});
  m_TriangleVertices.push_back({.Position = c, .Color = color});
}

void DebugGeometry::AddLightTarget(const DebugLightTarget& target) {
  if (target.Id != Engine::InvalidLightId) {
    m_LightTargets.push_back(target);
  }
}

void DebugGeometryBuilder::BuildScene(const Engine::Scene& scene, const DebugRenderSettings& settings,
                                      DebugGeometry& geometry) {
  geometry.Clear();

  if (settings.ShowWireframePlane) {
    AddWireframePlane(geometry);
  }

  if (settings.ShowLightGizmos) {
    scene.World().ForEachChunkReadOnly<Engine::Ecs::Light, Engine::Ecs::WorldTransform, Engine::Ecs::GlobalVisibility>(
        [&settings, &geometry](std::span<const Engine::EntityHandle> entities,
                               std::span<const Engine::Ecs::Light> lights,
                               std::span<const Engine::Ecs::WorldTransform> transforms,
                               std::span<const Engine::Ecs::GlobalVisibility> visibility) -> void {
          for (size_t row = 0; row < entities.size(); ++row) {
            if (!visibility[row].IsVisible) {
              continue;
            }
            AddLight(entities[row], lights[row], transforms[row],
                     static_cast<Engine::LightId>(entities[row].Index) + 1 == settings.SelectedLightId, geometry);
          }
        });
  }

  if (settings.ShowCameraFrustums) {
    scene.World().ForEachChunkReadOnly<Engine::Ecs::Camera, Engine::Ecs::WorldTransform, Engine::Ecs::GlobalVisibility>(
        [&geometry](std::span<const Engine::EntityHandle>, std::span<const Engine::Ecs::Camera> cameras,
                    std::span<const Engine::Ecs::WorldTransform> transforms,
                    std::span<const Engine::Ecs::GlobalVisibility> visibility) -> void {
          for (size_t row = 0; row < cameras.size(); ++row) {
            if (visibility[row].IsVisible) {
              AddCameraFrustum(cameras[row], transforms[row], geometry);
            }
          }
        });
  }
}

void DebugGeometryBuilder::AddCameraFrustum(const Engine::Ecs::Camera& camera,
                                            const Engine::Ecs::WorldTransform& transform, DebugGeometry& geometry) {
  if (!Engine::Ecs::Validate(camera)) {
    return;
  }
  DirectX::XMVECTOR scale;
  DirectX::XMVECTOR rotation;
  DirectX::XMVECTOR position;
  if (!DirectX::XMMatrixDecompose(&scale, &rotation, &position, DirectX::XMLoadFloat4x4(&transform.Matrix))) {
    return;
  }
  const float aspect = static_cast<float>(camera.OutputWidth) / static_cast<float>(camera.OutputHeight);
  const float tangent = std::tan(DirectX::XMConvertToRadians(camera.FieldOfViewDegrees) * 0.5F);
  const DirectX::XMVECTOR forward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0.0F, 0.0F, 1.0F, 0.0F), rotation);
  const DirectX::XMVECTOR right = DirectX::XMVector3Rotate(DirectX::XMVectorSet(1.0F, 0.0F, 0.0F, 0.0F), rotation);
  const DirectX::XMVECTOR up = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F), rotation);
  const auto planeCorners = [&](float distance) -> std::array<DirectX::XMFLOAT3, 4> {
    const float halfHeight = tangent * distance;
    const float halfWidth = halfHeight * aspect;
    const DirectX::XMVECTOR center = DirectX::XMVectorAdd(position, DirectX::XMVectorScale(forward, distance));
    const DirectX::XMVECTOR horizontal = DirectX::XMVectorScale(right, halfWidth);
    const DirectX::XMVECTOR vertical = DirectX::XMVectorScale(up, halfHeight);
    return {
        StoreFloat3(DirectX::XMVectorAdd(DirectX::XMVectorSubtract(center, horizontal), vertical)),
        StoreFloat3(DirectX::XMVectorAdd(DirectX::XMVectorAdd(center, horizontal), vertical)),
        StoreFloat3(DirectX::XMVectorSubtract(DirectX::XMVectorAdd(center, horizontal), vertical)),
        StoreFloat3(DirectX::XMVectorSubtract(DirectX::XMVectorSubtract(center, horizontal), vertical)),
    };
  };
  const auto nearCorners = planeCorners(camera.NearPlane);
  const auto farCorners = planeCorners(camera.FarPlane);
  const DirectX::XMFLOAT4 color(0.25F, 0.8F, 1.0F, 0.9F);
  for (size_t edge = 0; edge < 4; ++edge) {
    const size_t next = (edge + 1) % 4;
    geometry.AddLine(nearCorners[edge], nearCorners[next], color);
    geometry.AddLine(farCorners[edge], farCorners[next], color);
    geometry.AddLine(nearCorners[edge], farCorners[edge], color);
  }
}

void DebugGeometryBuilder::AddLight(Engine::EntityHandle entity, const Engine::Ecs::Light& light,
                                    const Engine::Ecs::WorldTransform& transform, bool selected,
                                    DebugGeometry& geometry) {
  const Engine::LightId id = static_cast<Engine::LightId>(entity.Index) + 1;
  DirectX::XMFLOAT3 position{};
  const DirectX::XMMATRIX worldMatrix = DirectX::XMLoadFloat4x4(&transform.Matrix);
  DirectX::XMStoreFloat3(&position, DirectX::XMVector3TransformCoord(DirectX::XMVectorZero(), worldMatrix));
  DirectX::XMFLOAT3 direction{};
  const DirectX::XMVECTOR localDirection =
      DirectX::XMVectorSet(light.Direction[0], light.Direction[1], light.Direction[2], 0.0F);
  DirectX::XMStoreFloat3(&direction,
                         DirectX::XMVector3TransformNormal(localDirection, DirectX::XMLoadFloat4x4(&transform.Matrix)));
  const DirectX::XMVECTOR origin = DirectX::XMLoadFloat3(&position);
  const DirectX::XMVECTOR normalizedDirection = NormalizeOrDefault(direction);

  const DirectX::XMFLOAT3 safeColor(light.Color[0], light.Color[1], light.Color[2]);
  const DirectX::XMFLOAT4 color = selected ? DirectX::XMFLOAT4(1.0F, 1.0F, 1.0F, 1.0F)
                                           : DirectX::XMFLOAT4(safeColor.x, safeColor.y, safeColor.z, 1.0F);
  const float safeRange =
      light.Type == Engine::Ecs::LightType::Directional ? 1.5F : std::clamp(light.Range, 0.25F, 1000.0F);
  const float outerAngleRadians =
      std::clamp(DirectX::XMConvertToRadians(light.OuterAngleDegrees), 0.0F, DirectX::XM_PIDIV2 - 0.001F);

  DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F);
  if (std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(up, normalizedDirection))) > 0.98F) {
    up = DirectX::XMVectorSet(1.0F, 0.0F, 0.0F, 0.0F);
  }
  const DirectX::XMVECTOR right = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(normalizedDirection, up));
  const DirectX::XMVECTOR side = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(right, normalizedDirection));

  if (light.Type == Engine::Ecs::LightType::Directional) {
    const DirectX::XMVECTOR tip = DirectX::XMVectorAdd(origin, DirectX::XMVectorScale(normalizedDirection, safeRange));
    const DirectX::XMVECTOR arrowBack =
        DirectX::XMVectorSubtract(tip, DirectX::XMVectorScale(normalizedDirection, 0.25F));
    geometry.AddLine(position, StoreFloat3(tip), color);
    geometry.AddLine(StoreFloat3(tip),
                     StoreFloat3(DirectX::XMVectorAdd(arrowBack, DirectX::XMVectorScale(right, 0.16F))), color);
    geometry.AddLine(StoreFloat3(tip),
                     StoreFloat3(DirectX::XMVectorSubtract(arrowBack, DirectX::XMVectorScale(right, 0.16F))), color);
    geometry.AddLine(StoreFloat3(tip),
                     StoreFloat3(DirectX::XMVectorAdd(arrowBack, DirectX::XMVectorScale(side, 0.16F))), color);
    geometry.AddLine(StoreFloat3(tip),
                     StoreFloat3(DirectX::XMVectorSubtract(arrowBack, DirectX::XMVectorScale(side, 0.16F))), color);
  } else if (light.Type == Engine::Ecs::LightType::Point) {
    for (int plane = 0; plane < 3; ++plane) {
      const DirectX::XMVECTOR firstAxis = plane == 2 ? side : right;
      const DirectX::XMVECTOR secondAxis = plane == 0 ? side : normalizedDirection;
      DirectX::XMFLOAT3 previous = {};
      for (uint32_t segment = 0; segment <= LightRingSegments; ++segment) {
        const float angle = TwoPi * static_cast<float>(segment) / static_cast<float>(LightRingSegments);
        const DirectX::XMVECTOR offset =
            DirectX::XMVectorAdd(DirectX::XMVectorScale(firstAxis, std::cos(angle) * safeRange),
                                 DirectX::XMVectorScale(secondAxis, std::sin(angle) * safeRange));
        const DirectX::XMFLOAT3 point = StoreFloat3(DirectX::XMVectorAdd(origin, offset));
        if (segment > 0) {
          geometry.AddLine(previous, point, color);
        }
        previous = point;
      }
    }
  } else {
    const float coneRadius = safeRange * std::tan(outerAngleRadians);
    const DirectX::XMVECTOR baseCenter =
        DirectX::XMVectorAdd(origin, DirectX::XMVectorScale(normalizedDirection, safeRange));
    DirectX::XMFLOAT3 previousRingPoint = {};
    for (uint32_t segment = 0; segment < LightRingSegments; ++segment) {
      const float angle = TwoPi * static_cast<float>(segment) / static_cast<float>(LightRingSegments);
      const DirectX::XMVECTOR ringOffset =
          DirectX::XMVectorAdd(DirectX::XMVectorScale(right, std::cos(angle) * coneRadius),
                               DirectX::XMVectorScale(side, std::sin(angle) * coneRadius));
      const DirectX::XMFLOAT3 ringPoint = StoreFloat3(DirectX::XMVectorAdd(baseCenter, ringOffset));

      if (segment > 0) {
        geometry.AddLine(previousRingPoint, ringPoint, color);
      }
      geometry.AddLine(position, ringPoint, color);
      previousRingPoint = ringPoint;
    }

    const DirectX::XMVECTOR firstRingOffset = DirectX::XMVectorScale(right, coneRadius);
    const DirectX::XMFLOAT3 firstRingPoint = StoreFloat3(DirectX::XMVectorAdd(baseCenter, firstRingOffset));
    geometry.AddLine(previousRingPoint, firstRingPoint, color);
    geometry.AddLine(position, StoreFloat3(baseCenter), color);

    const DirectX::XMFLOAT3 arrowTip =
        StoreFloat3(DirectX::XMVectorAdd(baseCenter, DirectX::XMVectorScale(normalizedDirection, safeRange * 0.08F)));
    const DirectX::XMVECTOR arrowSide = DirectX::XMVectorScale(right, safeRange * 0.08F);
    const DirectX::XMVECTOR arrowBack = DirectX::XMVectorSubtract(
        DirectX::XMLoadFloat3(&arrowTip), DirectX::XMVectorScale(normalizedDirection, safeRange * 0.16F));
    geometry.AddLine(arrowTip, StoreFloat3(DirectX::XMVectorAdd(arrowBack, arrowSide)), color);
    geometry.AddLine(arrowTip, StoreFloat3(DirectX::XMVectorSubtract(arrowBack, arrowSide)), color);
  }

  geometry.AddLightTarget({
      .Id = id,
      .Type = light.Type,
      .Position = position,
      .Direction = direction,
      .Range = safeRange,
      .OuterAngleRadians = outerAngleRadians,
  });
}

void DebugGeometryBuilder::AddWireframePlane(DebugGeometry& geometry) {
  const DirectX::XMFLOAT4 minorColor(0.22F, 0.26F, 0.30F, 0.45F);
  const DirectX::XMFLOAT4 majorColor(0.42F, 0.48F, 0.56F, 0.65F);
  const float step = (WireframePlaneExtent * 2.0F) / static_cast<float>(WireframePlaneDivisions);

  for (int index = 0; index <= WireframePlaneDivisions; ++index) {
    const float offset = -WireframePlaneExtent + (static_cast<float>(index) * step);
    const bool major = index == WireframePlaneDivisions / 2 || index % 5 == 0;
    const DirectX::XMFLOAT4& color = major ? majorColor : minorColor;

    geometry.AddLine({-WireframePlaneExtent, 0.0F, offset}, {WireframePlaneExtent, 0.0F, offset}, color);
    geometry.AddLine({offset, 0.0F, -WireframePlaneExtent}, {offset, 0.0F, WireframePlaneExtent}, color);
  }
}
} // namespace Engine::Renderer

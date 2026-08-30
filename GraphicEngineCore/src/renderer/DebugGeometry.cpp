#include "renderer/DebugGeometry.h"

#include "renderer/Light.h"

#include <algorithm>
#include <cmath>

namespace {
    constexpr float Pi = 3.14159265358979323846f;
    constexpr float TwoPi = 2.0f * Pi;
    constexpr uint32_t LightRingSegments = 32;
    constexpr float WireframePlaneExtent = 10.0f;
    constexpr int WireframePlaneDivisions = 20;

    DirectX::XMVECTOR NormalizeOrDefault(const DirectX::XMFLOAT3& value) {
        DirectX::XMVECTOR vector = DirectX::XMLoadFloat3(&value);
        const float lengthSquared = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(vector));
        if (!std::isfinite(lengthSquared) || lengthSquared < 1e-6f) {
            return DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
        }
        return DirectX::XMVector3Normalize(vector);
    }

    DirectX::XMFLOAT3 StoreFloat3(DirectX::FXMVECTOR value) {
        DirectX::XMFLOAT3 result;
        DirectX::XMStoreFloat3(&result, value);
        return result;
    }

}

namespace Renderer {

    void DebugGeometry::Clear() {
        m_LineVertices.clear();
        m_TriangleVertices.clear();
        m_LightTargets.clear();
    }

    void DebugGeometry::AddLine(const DirectX::XMFLOAT3& start,
        const DirectX::XMFLOAT3& end,
        const DirectX::XMFLOAT4& color) {
        m_LineVertices.push_back({ start, color });
        m_LineVertices.push_back({ end, color });
    }

    void DebugGeometry::AddTriangle(const DirectX::XMFLOAT3& a,
        const DirectX::XMFLOAT3& b,
        const DirectX::XMFLOAT3& c,
        const DirectX::XMFLOAT4& color) {
        m_TriangleVertices.push_back({ a, color });
        m_TriangleVertices.push_back({ b, color });
        m_TriangleVertices.push_back({ c, color });
    }

    void DebugGeometry::AddLightTarget(const DebugLightTarget& target) {
        if (target.Id != Engine::InvalidLightId) {
            m_LightTargets.push_back(target);
        }
    }

    void DebugGeometryBuilder::BuildScene(const Engine::Scene& scene,
        const DebugRenderSettings& settings,
        DebugGeometry& geometry) {
        geometry.Clear();

        if (settings.ShowWireframePlane) {
            AddWireframePlane(geometry);
        }

        if (!settings.ShowLightGizmos) {
            return;
        }

        const auto lights = scene.GetLights();
        for (size_t index = 0; index < lights.size(); ++index) {
            const auto& light = lights[index];
            if (!light) {
                continue;
            }

            const Engine::LightId lightId = scene.GetLightIdAt(index);
            AddSpotLight(lightId, *light, lightId == settings.SelectedLightId, geometry);
        }
    }

    void DebugGeometryBuilder::AddSpotLight(const Engine::LightId id,
        const SpotLight& light,
        bool selected,
        DebugGeometry& geometry) {
        const DirectX::XMFLOAT3 position = light.GetPosition();
        const DirectX::XMFLOAT3 direction = light.GetDirection();
        const DirectX::XMVECTOR origin = DirectX::XMLoadFloat3(&position);
        const DirectX::XMVECTOR normalizedDirection = NormalizeOrDefault(direction);

        const float safeRange = std::clamp(light.GetRange(), 0.25f, 1000.0f);
        const float outerAngleRadians = std::clamp(
            DirectX::XMConvertToRadians(light.GetOuterAngleDegrees()),
            0.0f,
            DirectX::XM_PIDIV2 - 0.001f);
        const float coneRadius = safeRange * std::tan(outerAngleRadians);

        DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        if (std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(up, normalizedDirection))) > 0.98f) {
            up = DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
        }
        const DirectX::XMVECTOR right = DirectX::XMVector3Normalize(
            DirectX::XMVector3Cross(normalizedDirection, up));
        const DirectX::XMVECTOR coneUp = DirectX::XMVector3Normalize(
            DirectX::XMVector3Cross(right, normalizedDirection));
        const DirectX::XMVECTOR baseCenter = DirectX::XMVectorAdd(
            origin,
            DirectX::XMVectorScale(normalizedDirection, safeRange));

        const DirectX::XMFLOAT3 safeColor = light.GetColor();
        const DirectX::XMFLOAT4 coneColor = selected
            ? DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f)
            : DirectX::XMFLOAT4(safeColor.x, safeColor.y, safeColor.z, 1.0f);
        DirectX::XMFLOAT3 previousRingPoint = {};
        for (uint32_t segment = 0; segment < LightRingSegments; ++segment) {
            const float angle = TwoPi * static_cast<float>(segment) /
                static_cast<float>(LightRingSegments);
            const DirectX::XMVECTOR ringOffset = DirectX::XMVectorAdd(
                DirectX::XMVectorScale(right, std::cos(angle) * coneRadius),
                DirectX::XMVectorScale(coneUp, std::sin(angle) * coneRadius));
            const DirectX::XMFLOAT3 ringPoint = StoreFloat3(DirectX::XMVectorAdd(baseCenter, ringOffset));

            if (segment > 0) {
                geometry.AddLine(previousRingPoint, ringPoint, coneColor);
            }
            geometry.AddLine(position, ringPoint, coneColor);
            previousRingPoint = ringPoint;
        }

        const DirectX::XMVECTOR firstRingOffset = DirectX::XMVectorScale(right, coneRadius);
        const DirectX::XMFLOAT3 firstRingPoint = StoreFloat3(DirectX::XMVectorAdd(baseCenter, firstRingOffset));
        geometry.AddLine(previousRingPoint, firstRingPoint, coneColor);
        geometry.AddLine(position, StoreFloat3(baseCenter), coneColor);

        const DirectX::XMFLOAT3 arrowTip = StoreFloat3(DirectX::XMVectorAdd(
            baseCenter,
            DirectX::XMVectorScale(normalizedDirection, safeRange * 0.08f)));
        const DirectX::XMVECTOR arrowSide = DirectX::XMVectorScale(right, safeRange * 0.08f);
        const DirectX::XMVECTOR arrowBack = DirectX::XMVectorSubtract(
            DirectX::XMLoadFloat3(&arrowTip),
            DirectX::XMVectorScale(normalizedDirection, safeRange * 0.16f));
        geometry.AddLine(arrowTip, StoreFloat3(DirectX::XMVectorAdd(arrowBack, arrowSide)), coneColor);
        geometry.AddLine(arrowTip, StoreFloat3(DirectX::XMVectorSubtract(arrowBack, arrowSide)), coneColor);

        geometry.AddLightTarget({ id, position, direction, safeRange, outerAngleRadians });
    }

    void DebugGeometryBuilder::AddWireframePlane(DebugGeometry& geometry) {
        const DirectX::XMFLOAT4 minorColor(0.22f, 0.26f, 0.30f, 0.45f);
        const DirectX::XMFLOAT4 majorColor(0.42f, 0.48f, 0.56f, 0.65f);
        const float step = (WireframePlaneExtent * 2.0f) /
            static_cast<float>(WireframePlaneDivisions);

        for (int index = 0; index <= WireframePlaneDivisions; ++index) {
            const float offset = -WireframePlaneExtent + static_cast<float>(index) * step;
            const bool major = index == WireframePlaneDivisions / 2 || index % 5 == 0;
            const DirectX::XMFLOAT4& color = major ? majorColor : minorColor;

            geometry.AddLine(
                { -WireframePlaneExtent, 0.0f, offset },
                { WireframePlaneExtent, 0.0f, offset },
                color);
            geometry.AddLine(
                { offset, 0.0f, -WireframePlaneExtent },
                { offset, 0.0f, WireframePlaneExtent },
                color);
        }
    }
}

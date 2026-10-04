#include "renderer/Light.h"
#include "renderer/Light.h"

#include <algorithm>
#include <cmath>

namespace Renderer {

    SpotLight::SpotLight(const DirectX::XMFLOAT3& direction, const DirectX::XMFLOAT3& color, float intensity)
        : m_Direction(direction), m_Color(color), m_Intensity(std::max(0.0f, intensity)) {
    }

    void SpotLight::SetDirection(const DirectX::XMFLOAT3& direction) {
        m_Direction = direction;
    }

    void SpotLight::LookAt(const DirectX::XMFLOAT3& target) {
        using namespace DirectX;

        const XMVECTOR position = XMLoadFloat3(&m_Position);
        const XMVECTOR targetPosition = XMLoadFloat3(&target);
        const XMVECTOR offset = XMVectorSubtract(targetPosition, position);
        const float lengthSquared = XMVectorGetX(XMVector3LengthSq(offset));
        if (!std::isfinite(lengthSquared) || lengthSquared < 1e-6f) {
            return;
        }

        XMStoreFloat3(&m_Direction, XMVector3Normalize(offset));
    }

    void SpotLight::SetPosition(const DirectX::XMFLOAT3& position) {
        m_Position = position;
    }

    void SpotLight::SetUsePosition(bool usePosition) {
        m_UsePosition = usePosition;
    }

    void SpotLight::SetColor(const DirectX::XMFLOAT3& color) {
        m_Color = color;
    }

    void SpotLight::SetIntensity(float intensity) {
        m_Intensity = std::max(0.0f, intensity);
    }

    void SpotLight::SetRange(float range) {
        m_Range = std::max(0.001f, range);
    }

    void SpotLight::SetConeAngles(float innerAngleDegrees, float outerAngleDegrees) {
        const float clampedInner = std::clamp(innerAngleDegrees, 0.0f, 89.0f);
        const float clampedOuter = std::clamp(outerAngleDegrees, 0.0f, 89.5f);

        m_InnerAngleDegrees = std::min(clampedInner, clampedOuter);
        m_OuterAngleDegrees = std::max(clampedInner, clampedOuter);
    }

    void SpotLight::SetCastShadows(bool castShadows) {
        m_CastShadows = castShadows;
    }

    void SpotLight::SetShadowBias(float bias) {
        m_ShadowBias = std::max(0.0f, bias);
    }

    void SpotLight::SetShadowNormalBias(float bias) {
        m_ShadowNormalBias = std::max(0.0f, bias);
    }

    DirectX::XMFLOAT3 SpotLight::GetDirection() const {
        return m_Direction;
    }

    DirectX::XMFLOAT3 SpotLight::GetPosition() const {
        return m_Position;
    }

    bool SpotLight::GetUsePosition() const {
        return m_UsePosition;
    }

    DirectX::XMFLOAT3 SpotLight::GetColor() const {
        return m_Color;
    }

    float SpotLight::GetIntensity() const {
        return m_Intensity;
    }

    float SpotLight::GetRange() const {
        return m_Range;
    }

    float SpotLight::GetInnerAngleDegrees() const {
        return m_InnerAngleDegrees;
    }

    float SpotLight::GetOuterAngleDegrees() const {
        return m_OuterAngleDegrees;
    }

    bool SpotLight::GetCastShadows() const {
        return m_CastShadows;
    }

    float SpotLight::GetShadowBias() const {
        return m_ShadowBias;
    }

    float SpotLight::GetShadowNormalBias() const {
        return m_ShadowNormalBias;
    }

    DirectionalLightData SpotLight::GetDirectionalLightData() const {
        DirectionalLightData data;

        const float innerRad = m_InnerAngleDegrees * 0.017453292519943295769f;
        const float outerRad = m_OuterAngleDegrees * 0.017453292519943295769f;

        data.DirectionIntensity = { m_Direction.x, m_Direction.y, m_Direction.z, m_Intensity };
        data.ColorUsePosition = { m_Color.x, m_Color.y, m_Color.z, m_UsePosition ? 1.0f : 0.0f };
        data.PositionRange = { m_Position.x, m_Position.y, m_Position.z, m_Range };
        data.ConeParams = { std::cos(innerRad), std::cos(outerRad), 0.0f, 0.0f };
        data.ShadowParams = { -1.0f, m_ShadowBias, m_ShadowNormalBias, m_CastShadows ? 1.0f : 0.0f };

        return data;
    }
}

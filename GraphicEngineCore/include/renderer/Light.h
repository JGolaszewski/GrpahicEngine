#pragma once
#pragma once

#include "CoreExport.h"
#include <DirectXMath.h>

namespace Renderer {

    struct ENGINE_API DirectionalLightData {
        DirectX::XMFLOAT4 DirectionIntensity = { 0.0f, -1.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT4 ColorUsePosition = { 1.0f, 1.0f, 1.0f, 0.0f };
        DirectX::XMFLOAT4 PositionRange = { 0.0f, 0.0f, 0.0f, 20.0f };
        DirectX::XMFLOAT4 ConeParams = { 0.98f, 0.92f, 0.0f, 0.0f };
        DirectX::XMFLOAT4X4 LightViewProj = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        DirectX::XMFLOAT4 ShadowParams = { -1.0f, 0.0015f, 0.0015f, 0.0f };
    };

    class ENGINE_API SpotLight {
    public:
        SpotLight() = default;
        SpotLight(const DirectX::XMFLOAT3& direction, const DirectX::XMFLOAT3& color, float intensity);

        void SetDirection(const DirectX::XMFLOAT3& direction);
        void SetPosition(const DirectX::XMFLOAT3& position);
        void SetUsePosition(bool usePosition);
        void SetColor(const DirectX::XMFLOAT3& color);
        void SetIntensity(float intensity);
        void SetRange(float range);
        void SetConeAngles(float innerAngleDegrees, float outerAngleDegrees);
        void SetCastShadows(bool castShadows);
        void SetShadowBias(float bias);
        void SetShadowNormalBias(float bias);

        DirectX::XMFLOAT3 GetDirection() const;
        DirectX::XMFLOAT3 GetPosition() const;
        bool GetUsePosition() const;
        DirectX::XMFLOAT3 GetColor() const;
        float GetIntensity() const;
        float GetRange() const;
        float GetInnerAngleDegrees() const;
        float GetOuterAngleDegrees() const;
        bool GetCastShadows() const;
        float GetShadowBias() const;
        float GetShadowNormalBias() const;

        DirectionalLightData GetDirectionalLightData() const;

    private:
        DirectX::XMFLOAT3 m_Direction = { 0.0f, -1.0f, 0.0f };
        DirectX::XMFLOAT3 m_Position = { 0.0f, 3.0f, 0.0f };
        bool m_UsePosition = false;
        DirectX::XMFLOAT3 m_Color = { 1.0f, 1.0f, 1.0f };
        float m_Intensity = 1.0f;
        float m_Range = 20.0f;
        float m_InnerAngleDegrees = 15.0f;
        float m_OuterAngleDegrees = 25.0f;
        bool m_CastShadows = true;
        float m_ShadowBias = 0.0015f;
        float m_ShadowNormalBias = 0.0015f;
    };
}

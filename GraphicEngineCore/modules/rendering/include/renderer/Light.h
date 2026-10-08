#pragma once
#pragma once

#include "runtime/include/CoreExport.h"
#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Renderer
{

#ifndef ENGINE_MAX_LIGHTS
#define ENGINE_MAX_LIGHTS 8
#endif

struct ENGINE_API DirectionalLightData
{
    DirectX::XMFLOAT4 DirectionIntensity = {0.0f, -1.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4 ColorUsePosition = {1.0f, 1.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT4 PositionRange = {0.0f, 0.0f, 0.0f, 20.0f};
    DirectX::XMFLOAT4 ConeParams = {0.98f, 0.92f, 0.0f, 0.0f};
    DirectX::XMFLOAT4X4 LightViewProj = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                         0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4 ShadowParams = {-1.0f, 0.0015f, 0.0015f, 0.0f};
};

struct ENGINE_API LightsCBData
{
    DirectionalLightData Lights[ENGINE_MAX_LIGHTS]{};
    uint32_t LightCount = 0;
    float Padding[3] = {0.0f, 0.0f, 0.0f};
};

static_assert(sizeof(DirectionalLightData) == 144,
              "DirectionalLightData must match the HLSL LightData layout");
static_assert(offsetof(DirectionalLightData, DirectionIntensity) == 0);
static_assert(offsetof(DirectionalLightData, ColorUsePosition) == 16);
static_assert(offsetof(DirectionalLightData, PositionRange) == 32);
static_assert(offsetof(DirectionalLightData, ConeParams) == 48);
static_assert(offsetof(DirectionalLightData, LightViewProj) == 64);
static_assert(offsetof(DirectionalLightData, ShadowParams) == 128);
static_assert(offsetof(LightsCBData, LightCount) == ENGINE_MAX_LIGHTS * sizeof(DirectionalLightData));
static_assert(sizeof(LightsCBData) == ENGINE_MAX_LIGHTS * sizeof(DirectionalLightData) + 16);

class ENGINE_API SpotLight
{
  public:
    SpotLight() = default;
    SpotLight(const DirectX::XMFLOAT3 &direction, const DirectX::XMFLOAT3 &color, float intensity);

    void SetDirection(const DirectX::XMFLOAT3 &direction);
    void LookAt(const DirectX::XMFLOAT3 &target);
    void SetPosition(const DirectX::XMFLOAT3 &position);
    void SetUsePosition(bool usePosition);
    void SetColor(const DirectX::XMFLOAT3 &color);
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
    DirectX::XMFLOAT3 m_Direction = {0.0f, -1.0f, 0.0f};
    DirectX::XMFLOAT3 m_Position = {0.0f, 3.0f, 0.0f};
    bool m_UsePosition = false;
    DirectX::XMFLOAT3 m_Color = {1.0f, 1.0f, 1.0f};
    float m_Intensity = 1.0f;
    float m_Range = 20.0f;
    float m_InnerAngleDegrees = 15.0f;
    float m_OuterAngleDegrees = 25.0f;
    bool m_CastShadows = true;
    float m_ShadowBias = 0.0015f;
    float m_ShadowNormalBias = 0.0015f;
};
} // namespace Engine::Renderer

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

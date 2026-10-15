#pragma once

#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>

#ifndef ENGINE_MAX_LIGHTS
#  define ENGINE_MAX_LIGHTS 8
#endif

namespace Engine::Renderer {
struct DirectionalLightData {
  DirectX::XMFLOAT4 DirectionIntensity = {0.0f, -1.0f, 0.0f, 1.0f};
  DirectX::XMFLOAT4 ColorUsePosition = {1.0f, 1.0f, 1.0f, 0.0f};
  DirectX::XMFLOAT4 PositionRange = {0.0f, 0.0f, 0.0f, 20.0f};
  DirectX::XMFLOAT4 ConeParams = {0.98f, 0.92f, 0.0f, 0.0f};
  DirectX::XMFLOAT4X4 LightViewProj = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                       0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  DirectX::XMFLOAT4 ShadowParams = {-1.0f, 0.0015f, 0.0015f, 0.0f};
};

struct LightsCBData {
  DirectionalLightData Lights[ENGINE_MAX_LIGHTS]{};
  uint32_t LightCount = 0;
  float Padding[3] = {0.0f, 0.0f, 0.0f};
};

static_assert(sizeof(DirectionalLightData) == 144);
static_assert(offsetof(DirectionalLightData, DirectionIntensity) == 0);
static_assert(offsetof(DirectionalLightData, ColorUsePosition) == 16);
static_assert(offsetof(DirectionalLightData, PositionRange) == 32);
static_assert(offsetof(DirectionalLightData, ConeParams) == 48);
static_assert(offsetof(DirectionalLightData, LightViewProj) == 64);
static_assert(offsetof(DirectionalLightData, ShadowParams) == 128);
static_assert(offsetof(LightsCBData, LightCount) == ENGINE_MAX_LIGHTS * sizeof(DirectionalLightData));
static_assert(sizeof(LightsCBData) == ENGINE_MAX_LIGHTS * sizeof(DirectionalLightData) + 16);
} // namespace Engine::Renderer

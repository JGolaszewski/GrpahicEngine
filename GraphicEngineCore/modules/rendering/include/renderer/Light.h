#pragma once

#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>

#ifndef ENGINE_MAX_LIGHTS
#  define ENGINE_MAX_LIGHTS 8
#endif

namespace Engine::Renderer {
/// Per-light data matching the shader constant-buffer layout.
struct DirectionalLightData {
  DirectX::XMFLOAT4 DirectionIntensity = {0.0f, -1.0f, 0.0f, 1.0f}; ///< Direction XYZ and intensity.
  DirectX::XMFLOAT4 ColorUsePosition = {1.0f, 1.0f, 1.0f, 0.0f}; ///< RGB color and positional-light flag.
  DirectX::XMFLOAT4 PositionRange = {0.0f, 0.0f, 0.0f, 20.0f}; ///< Position XYZ and influence range.
  DirectX::XMFLOAT4 ConeParams = {0.98f, 0.92f, 0.0f, 0.0f}; ///< Inner and outer spotlight cone cosines.
  DirectX::XMFLOAT4X4 LightViewProj = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                       0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}; ///< Light view-projection matrix.
  DirectX::XMFLOAT4 ShadowParams = {-1.0f, 0.0015f, 0.0015f, 0.0f}; ///< Shadow enable, depth bias, normal bias, padding.
};

/// Array of active light data and its count for the shader constant buffer.
struct LightsCBData {
  DirectionalLightData Lights[ENGINE_MAX_LIGHTS]{}; ///< Shader data for each light slot.
  uint32_t LightCount = 0;                          ///< Number of populated light slots.
  uint32_t EnvironmentEnabled = 0;                  ///< Whether this view adds environment lighting.
  float EnvironmentIntensity = 1.0F;                ///< Scale applied to the environment contribution.
  float AmbientLightIntensity = 0.0F;               ///< Scene ambient diffuse intensity, or zero when disabled.
  DirectX::XMFLOAT4 AmbientLightColor = {1.0F, 1.0F, 1.0F, 0.0F}; ///< Linear ambient color and padding.
};

static_assert(sizeof(DirectionalLightData) == 144);
static_assert(offsetof(DirectionalLightData, DirectionIntensity) == 0);
static_assert(offsetof(DirectionalLightData, ColorUsePosition) == 16);
static_assert(offsetof(DirectionalLightData, PositionRange) == 32);
static_assert(offsetof(DirectionalLightData, ConeParams) == 48);
static_assert(offsetof(DirectionalLightData, LightViewProj) == 64);
static_assert(offsetof(DirectionalLightData, ShadowParams) == 128);
static_assert(offsetof(LightsCBData, LightCount) == ENGINE_MAX_LIGHTS * sizeof(DirectionalLightData));
static_assert(offsetof(LightsCBData, EnvironmentEnabled) == offsetof(LightsCBData, LightCount) + sizeof(uint32_t));
static_assert(offsetof(LightsCBData, EnvironmentIntensity) == offsetof(LightsCBData, EnvironmentEnabled) + sizeof(uint32_t));
static_assert(offsetof(LightsCBData, AmbientLightIntensity) ==
              offsetof(LightsCBData, EnvironmentIntensity) + sizeof(float));
static_assert(offsetof(LightsCBData, AmbientLightColor) ==
              offsetof(LightsCBData, AmbientLightIntensity) + sizeof(float));
static_assert(sizeof(LightsCBData) == ENGINE_MAX_LIGHTS * sizeof(DirectionalLightData) + 32);
} // namespace Engine::Renderer

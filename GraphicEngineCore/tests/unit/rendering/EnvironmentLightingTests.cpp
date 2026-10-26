#include "rendering/include/renderer/Light.h"
#include "rendering/include/renderer/ViewportBinding.h"

#include <gtest/gtest.h>

#include <cstddef>

TEST(EnvironmentLightingTests, ViewEnvironmentDefaultsToDisabledWithUnitIntensity) {
  const Engine::EnvironmentLightingSettings environment;

  EXPECT_EQ(environment.Preset, Engine::EnvironmentPreset::Disabled);
  EXPECT_FLOAT_EQ(environment.Intensity, 1.0F);
}

TEST(EnvironmentLightingTests, EnvironmentControlsUseTheExistingTrailingConstantBufferRegister) {
  using Engine::Renderer::DirectionalLightData;
  using Engine::Renderer::LightsCBData;

  EXPECT_EQ(offsetof(LightsCBData, EnvironmentEnabled), offsetof(LightsCBData, LightCount) + sizeof(uint32_t));
  EXPECT_EQ(offsetof(LightsCBData, EnvironmentIntensity),
            offsetof(LightsCBData, EnvironmentEnabled) + sizeof(uint32_t));
  EXPECT_EQ(offsetof(LightsCBData, AmbientLightIntensity),
            offsetof(LightsCBData, EnvironmentIntensity) + sizeof(float));
  EXPECT_EQ(offsetof(LightsCBData, AmbientLightColor), offsetof(LightsCBData, AmbientLightIntensity) + sizeof(float));
  EXPECT_EQ(sizeof(LightsCBData) - offsetof(LightsCBData, LightCount), sizeof(uint32_t) * 8U);
  EXPECT_EQ(sizeof(DirectionalLightData), 144U);
}

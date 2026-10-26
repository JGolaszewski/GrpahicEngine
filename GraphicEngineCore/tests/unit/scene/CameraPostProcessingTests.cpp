#include "rendering/include/renderer/ViewportBinding.h"
#include "scene/include/Scene.h"
#include "scene/include/components/CameraPostProcessing.h"

#include <gtest/gtest.h>

#include <limits>

TEST(CameraPostProcessingTests, EmptyAndSupportedBoundaryValuesPassValidation) {
  EXPECT_TRUE(Engine::Ecs::Validate(Engine::Ecs::CameraPostProcessing{}));
  const Engine::Ecs::CameraPostProcessing legacyAggregate{
      std::vector<Engine::Ecs::CameraPostProcessEffect>{Engine::Ecs::ColorGradingEffect{}}};
  ASSERT_EQ(legacyAggregate.Effects.size(), 1U);
  EXPECT_FALSE(legacyAggregate.AmbientOcclusion.has_value());

  const Engine::Ecs::CameraPostProcessing supported{
      .Effects = {Engine::Ecs::ColorGradingEffect{.Exposure = -16.0F, .Contrast = 4.0F, .Saturation = 0.0F},
                  Engine::Ecs::BloomEffect{.Threshold = 100000.0F, .Intensity = 10.0F, .Radius = 64.0F},
                  Engine::Ecs::FxaaEffect{}},
      .AmbientOcclusion = Engine::Ecs::ScreenSpaceAmbientOcclusionSettings{.Radius = 5.0F, .Intensity = 4.0F}};
  EXPECT_TRUE(Engine::Ecs::Validate(supported));
}

TEST(CameraPostProcessingTests, NonFiniteOrOutOfRangeValuesFailValidation) {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(Engine::Ecs::Validate(
      Engine::Ecs::CameraPostProcessing{.Effects = {Engine::Ecs::ColorGradingEffect{.Exposure = nan}}}));
  EXPECT_FALSE(Engine::Ecs::Validate(
      Engine::Ecs::CameraPostProcessing{.Effects = {Engine::Ecs::ColorGradingEffect{.Exposure = 16.01F}}}));
  EXPECT_FALSE(Engine::Ecs::Validate(
      Engine::Ecs::CameraPostProcessing{.Effects = {Engine::Ecs::BloomEffect{.Radius = 64.01F}}}));
  EXPECT_FALSE(Engine::Ecs::Validate(Engine::Ecs::CameraPostProcessing{
      .AmbientOcclusion = Engine::Ecs::ScreenSpaceAmbientOcclusionSettings{.Radius = nan}}));
  EXPECT_FALSE(Engine::Ecs::Validate(Engine::Ecs::CameraPostProcessing{
      .AmbientOcclusion = Engine::Ecs::ScreenSpaceAmbientOcclusionSettings{.Intensity = 4.01F}}));
}

TEST(CameraPostProcessingTests, ViewportBindingRejectsInvalidEffectSettings) {
  Engine::Scene scene;
  int framebufferToken = 0;
  Engine::Viewport viewport(1U, 1U, &framebufferToken);
  Engine::ViewportBinding binding;
  binding.TargetScene = &scene;
  binding.TargetViewport = &viewport;
  binding.PostProcessing.Effects.emplace_back(
      Engine::Ecs::BloomEffect{.Threshold = 1.0F, .Intensity = 0.25F, .Radius = 65.0F});

  EXPECT_FALSE(binding.IsValid());
}

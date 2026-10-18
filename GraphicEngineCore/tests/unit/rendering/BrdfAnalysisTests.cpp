#include "rendering/include/renderer/brdf/BrdfAnalysis.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace {

using Engine::Renderer::BrdfAnalysis::AnalyzeBrdfResponse;
using Engine::Renderer::BrdfAnalysis::MeasureSceneLuminance;

TEST(BrdfAnalysisTests, SamplesFiniteCurvesAndComputesSummaryMeasurements) {
  Engine::Renderer::MaterialProperties material;
  material.Model = Engine::Renderer::BrdfModel::GGX;
  material.Roughness = 0.4F;
  const auto result = AnalyzeBrdfResponse(material, Engine::Renderer::BrdfModel::GGX, 91);

  ASSERT_TRUE(result.has_value());
  const auto& curve = *result;
  ASSERT_EQ(curve.AnglesDegrees.size(), 91U);
  ASSERT_EQ(curve.Response.size(), 91U);
  EXPECT_FLOAT_EQ(curve.AnglesDegrees.front(), 0.0F);
  EXPECT_NEAR(curve.AnglesDegrees.back(), 90.0F, 1.0e-5F);
  float peak = 0.0F;
  float mean = 0.0F;
  float integral = 0.0F;
  for (const float response : curve.Response) {
    EXPECT_TRUE(std::isfinite(response));
    EXPECT_GE(response, 0.0F);
    peak = (std::max)(peak, response);
    mean += response / static_cast<float>(curve.Response.size());
  }
  for (size_t index = 1; index < curve.Response.size(); ++index) {
    const float deltaRadians =
        (curve.AnglesDegrees[index] - curve.AnglesDegrees[index - 1]) * (std::numbers::pi_v<float> / 180.0F);
    integral += (curve.Response[index - 1] + curve.Response[index]) * 0.5F * deltaRadians;
  }
  EXPECT_FLOAT_EQ(curve.Measurements.Peak, peak);
  EXPECT_NEAR(curve.Measurements.Mean, mean, 1.0e-6F);
  EXPECT_NEAR(curve.Measurements.Integral, integral, 1.0e-6F);
}

TEST(BrdfAnalysisTests, SupportsLegacyGgxAndGtrWithFiniteInputs) {
  Engine::Renderer::MaterialProperties material;
  material.Roughness = 0.18F;
  material.GtrGamma = 4.0F;
  for (const Engine::Renderer::BrdfModel model :
       {Engine::Renderer::BrdfModel::Legacy, Engine::Renderer::BrdfModel::GGX, Engine::Renderer::BrdfModel::GTR}) {
    const auto result = AnalyzeBrdfResponse(material, model, 13);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::isfinite(result->Measurements.Peak));
    EXPECT_TRUE(std::isfinite(result->Measurements.Mean));
    EXPECT_TRUE(std::isfinite(result->Measurements.Integral));
  }
}

TEST(BrdfAnalysisTests, UsesTheSameAngularInputsForEverySupportedModel) {
  const Engine::Renderer::MaterialProperties material;
  const auto legacy = AnalyzeBrdfResponse(material, Engine::Renderer::BrdfModel::Legacy, 17);
  const auto ggx = AnalyzeBrdfResponse(material, Engine::Renderer::BrdfModel::GGX, 17);
  const auto gtr = AnalyzeBrdfResponse(material, Engine::Renderer::BrdfModel::GTR, 17);

  ASSERT_TRUE(legacy.has_value());
  ASSERT_TRUE(ggx.has_value());
  ASSERT_TRUE(gtr.has_value());
  EXPECT_EQ(legacy->AnglesDegrees, ggx->AnglesDegrees);
  EXPECT_EQ(legacy->AnglesDegrees, gtr->AnglesDegrees);
}

TEST(BrdfAnalysisTests, RejectsInvalidCurveSampleCountAndModel) {
  const Engine::Renderer::MaterialProperties material;
  EXPECT_FALSE(AnalyzeBrdfResponse(material, Engine::Renderer::BrdfModel::GGX, 1).has_value());
  EXPECT_FALSE(AnalyzeBrdfResponse(material, Engine::Renderer::BrdfModel::GGX, 4097).has_value());
  EXPECT_FALSE(AnalyzeBrdfResponse(material, static_cast<Engine::Renderer::BrdfModel>(99), 2).has_value());
}

TEST(BrdfAnalysisTests, UsesSanitizedInputsWithoutMutatingAuthoredMaterial) {
  Engine::Renderer::MaterialProperties material;
  material.Roughness = std::numeric_limits<float>::quiet_NaN();
  material.GtrGamma = std::numeric_limits<float>::infinity();
  material.BaseColor.x = std::numeric_limits<float>::quiet_NaN();
  material.Metallic = std::numeric_limits<float>::infinity();
  const auto originalRoughness = material.Roughness;
  const auto originalGamma = material.GtrGamma;
  const auto originalRed = material.BaseColor.x;
  const auto originalMetallic = material.Metallic;

  const auto result = AnalyzeBrdfResponse(material, Engine::Renderer::BrdfModel::GTR, 21);

  ASSERT_TRUE(result.has_value());
  for (const float response : result->Response) {
    EXPECT_TRUE(std::isfinite(response));
  }
  EXPECT_TRUE(std::isnan(material.Roughness));
  EXPECT_TRUE(std::isinf(material.GtrGamma));
  EXPECT_TRUE(std::isnan(material.BaseColor.x));
  EXPECT_TRUE(std::isinf(material.Metallic));
  EXPECT_TRUE(std::isnan(originalRoughness));
  EXPECT_TRUE(std::isinf(originalGamma));
  EXPECT_TRUE(std::isnan(originalRed));
  EXPECT_TRUE(std::isinf(originalMetallic));
}

TEST(BrdfAnalysisTests, MeasuresLinearRec709SceneMeanAndPeakLuminance) {
  constexpr std::array<float, 8> rgba = {
      1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 0.25F,
  };
  const auto result = MeasureSceneLuminance(2, 1, rgba);

  ASSERT_TRUE(result.has_value());
  EXPECT_NEAR(result->Mean, (0.2126F + 0.7152F) * 0.5F, 1.0e-6F);
  EXPECT_FLOAT_EQ(result->Peak, 0.7152F);
}

TEST(BrdfAnalysisTests, RejectsMalformedOrNonFiniteScenePixels) {
  constexpr std::array<float, 4> pixel = {0.1F, 0.2F, 0.3F, 1.0F};
  EXPECT_FALSE(MeasureSceneLuminance(0, 1, pixel).has_value());
  EXPECT_FALSE(MeasureSceneLuminance(2, 1, pixel).has_value());

  auto invalidPixel = pixel;
  invalidPixel[2] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(MeasureSceneLuminance(1, 1, invalidPixel).has_value());
}

} // namespace

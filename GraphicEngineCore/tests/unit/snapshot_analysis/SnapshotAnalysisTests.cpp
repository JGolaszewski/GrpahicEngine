#include "analysis/SnapshotAnalysis.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {
using Engine::SnapshotAnalysis::ColorInterpretation;
using Engine::SnapshotAnalysis::ColorPrimaries;
using Engine::SnapshotAnalysis::DifferenceMetric;
using Engine::SnapshotAnalysis::LuminanceUnit;
using Engine::SnapshotAnalysis::PixelRect;
using Engine::SnapshotAnalysis::PlaneSemantic;
using Engine::SnapshotAnalysis::PlaneView;
using Engine::SnapshotAnalysis::RoiHistogramOptions;
using Engine::SnapshotAnalysis::ScalarFormat;
using Engine::SnapshotAnalysis::TransferFunction;

ColorInterpretation LinearRec709(std::optional<double> nitsPerUnit = std::nullopt) {
  return {ColorPrimaries::Rec709, TransferFunction::Linear, nitsPerUnit};
}

PlaneView ColorPlane(std::span<const float> samples, std::uint32_t width, std::uint32_t height,
                     ColorInterpretation color = LinearRec709()) {
  return {width, height, 4, PlaneSemantic::FinalColor, ScalarFormat::Float32, color, samples, {}};
}

} // namespace

TEST(SnapshotRoiHistogramTests, UsesHalfOpenSourceSpaceRoiAndLinearRec709Luminance) {
  const std::array<float, 16> pixels{1.0F, 0.0F, 0.0F, 1.0F, 0.0F,  1.0F,  0.0F,  1.0F,
                                     0.0F, 0.0F, 1.0F, 1.0F, 0.25F, 0.25F, 0.25F, 1.0F};
  const PlaneView plane = ColorPlane(pixels, 2, 2);
  RoiHistogramOptions options{{1, 0, 2, 2}, 4, 0.0F, 1.0F};

  const auto histogram = Engine::SnapshotAnalysis::ComputeRoiHistogram(plane, options);

  ASSERT_TRUE(histogram.has_value()) << histogram.error().Message;
  EXPECT_EQ(histogram->ValidPixelCount, 2);
  EXPECT_EQ(histogram->InvalidSampleCount, 0);
  EXPECT_EQ(histogram->LuminanceUnits, LuminanceUnit::SceneLinearRelative);
  EXPECT_EQ(histogram->Bins[0], (std::vector<std::uint64_t>{1, 1, 0, 0}));
  EXPECT_EQ(histogram->Bins[1], (std::vector<std::uint64_t>{0, 1, 0, 1}));
  EXPECT_EQ(histogram->Bins[2], (std::vector<std::uint64_t>{1, 1, 0, 0}));
  // Blue luminance is 0.0722 and the gray sample is 0.25.
  EXPECT_EQ(histogram->Bins[3], (std::vector<std::uint64_t>{0, 1, 1, 0}));
}

TEST(SnapshotRoiHistogramTests, ExcludesAndCountsInvalidPixelsAndUsesNitsOnlyWithCalibration) {
  const std::array<float, 8> pixels{2.0F, -1.0F, 0.5F, 1.0F, std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 1.0F};
  const PlaneView plane = ColorPlane(pixels, 2, 1, LinearRec709(100.0));
  RoiHistogramOptions options{{0, 0, 2, 1}, 2, 0.0F, 1.0F};

  const auto histogram = Engine::SnapshotAnalysis::ComputeRoiHistogram(plane, options);

  ASSERT_TRUE(histogram.has_value()) << histogram.error().Message;
  EXPECT_EQ(histogram->ValidPixelCount, 1);
  EXPECT_EQ(histogram->InvalidSampleCount, 1);
  EXPECT_EQ(histogram->LuminanceUnits, LuminanceUnit::Nits);
  EXPECT_EQ(histogram->OutOfRangeValueCount, 3);
  EXPECT_EQ(histogram->Bins[0][1], 1); // Values above maximum clamp into the last bin.
  EXPECT_EQ(histogram->Bins[1][0], 1); // Values below minimum clamp into the first bin.
}

TEST(SnapshotRoiHistogramTests, RejectsInvalidRoisAndNonlinearOrNonRec709Color) {
  const std::array<float, 4> pixel{0.1F, 0.2F, 0.3F, 1.0F};
  RoiHistogramOptions options{{0, 0, 1, 1}, 16, 0.0F, 1.0F};
  EXPECT_FALSE(Engine::SnapshotAnalysis::ComputeRoiHistogram(
                   ColorPlane(pixel, 1, 1, {ColorPrimaries::Rec709, TransferFunction::SRgb, std::nullopt}), options)
                   .has_value());
  options.Region.Right = 2;
  EXPECT_FALSE(Engine::SnapshotAnalysis::ComputeRoiHistogram(ColorPlane(pixel, 1, 1), options).has_value());
  EXPECT_FALSE(Engine::SnapshotAnalysis::ComputeRoiHistogram(ColorPlane(pixel, 1, 1, LinearRec709(0.0)),
                                                             {{0, 0, 1, 1}, 16, 0.0F, 1.0F})
                   .has_value());
}

TEST(SnapshotComparisonTests, ComputesReferenceDirectedMetricsAndLeavesHeatmapClampingDisplayOnly) {
  const std::array<float, 4> reference{1.0F, 2.0F, 3.0F, 4.0F};
  const std::array<float, 4> comparison{2.0F, 4.0F, 6.0F, 10.0F};
  const PlaneView a = ColorPlane(reference, 1, 1);
  const PlaneView b = ColorPlane(comparison, 1, 1);

  const auto absolute = Engine::SnapshotAnalysis::ComputeComparison(a, b, DifferenceMetric::MeanAbsolute);
  ASSERT_TRUE(absolute.has_value()) << absolute.error().Message;
  ASSERT_EQ(absolute->ChannelAbsoluteErrors.size(), 4);
  EXPECT_DOUBLE_EQ(absolute->ChannelAbsoluteErrors[0], 1.0);
  EXPECT_DOUBLE_EQ(absolute->ChannelAbsoluteErrors[3], 6.0);
  EXPECT_DOUBLE_EQ(absolute->ScalarValues[0], 2.0); // RGB mean; alpha is not part of the display map.
  EXPECT_DOUBLE_EQ(absolute->Statistics.Mean, 2.0);
  EXPECT_DOUBLE_EQ(absolute->Statistics.Maximum, 2.0);
  EXPECT_DOUBLE_EQ(absolute->Statistics.Rms, 2.0);

  const auto rms = Engine::SnapshotAnalysis::ComputeComparison(a, b, DifferenceMetric::RgbRms);
  ASSERT_TRUE(rms.has_value()) << rms.error().Message;
  EXPECT_NEAR(rms->ScalarValues[0], std::sqrt(14.0 / 3.0), 1.0e-12);

  const auto relative = Engine::SnapshotAnalysis::ComputeComparison(a, b, DifferenceMetric::RelativeLuminance);
  ASSERT_TRUE(relative.has_value()) << relative.error().Message;
  EXPECT_NEAR(relative->ScalarValues[0], 1.0, 1.0e-12);
  const auto reversed = Engine::SnapshotAnalysis::ComputeComparison(b, a, DifferenceMetric::RelativeLuminance);
  ASSERT_TRUE(reversed.has_value()) << reversed.error().Message;
  EXPECT_NEAR(reversed->ScalarValues[0], 0.5, 1.0e-12);

  const auto display = Engine::SnapshotAnalysis::MakeHeatmapDisplayValues(*absolute, 0.5, 1.5);
  ASSERT_TRUE(display.has_value()) << display.error().Message;
  EXPECT_FLOAT_EQ((*display)[0], 1.0F);
  EXPECT_DOUBLE_EQ(absolute->ScalarValues[0], 2.0); // Raw analysis remains unclamped.
}

TEST(SnapshotComparisonTests, ExcludesNonfinitePixelsAndReportsStatisticsOverValidSamplesOnly) {
  const std::array<float, 8> reference{0.0F, 0.0F, 0.0F, 1.0F, std::numeric_limits<float>::infinity(),
                                       0.0F, 0.0F, 1.0F};
  const std::array<float, 8> comparison{1.0F, 2.0F, 3.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F};
  const auto difference = Engine::SnapshotAnalysis::ComputeComparison(
      ColorPlane(reference, 2, 1), ColorPlane(comparison, 2, 1), DifferenceMetric::MeanAbsolute);

  ASSERT_TRUE(difference.has_value()) << difference.error().Message;
  EXPECT_EQ(difference->Statistics.ValidSampleCount, 1);
  EXPECT_EQ(difference->Statistics.InvalidSampleCount, 1);
  EXPECT_FALSE(std::isfinite(difference->ScalarValues[1]));
  EXPECT_EQ(difference->ValidSamples[1], 0);
  EXPECT_DOUBLE_EQ(difference->Statistics.Mean, 2.0);
}

TEST(SnapshotComparisonTests, ReportsCompatibilityFailuresWithoutResamplingOrReinterpreting) {
  const std::array<float, 4> pixels{0.0F, 0.0F, 0.0F, 1.0F};
  const auto a = ColorPlane(pixels, 1, 1);
  const auto wrongSize = ColorPlane(pixels, 2, 1);
  EXPECT_FALSE(Engine::SnapshotAnalysis::ComputeComparison(a, wrongSize, DifferenceMetric::MeanAbsolute).has_value());
  const auto wrongColor = ColorPlane(pixels, 1, 1, {ColorPrimaries::Rec709, TransferFunction::SRgb, std::nullopt});
  const auto colorMismatch = Engine::SnapshotAnalysis::ComputeComparison(a, wrongColor, DifferenceMetric::MeanAbsolute);
  ASSERT_FALSE(colorMismatch.has_value());
  EXPECT_NE(colorMismatch.error().Message.find("color interpretation"), std::string::npos);
  const auto differentCalibration = ColorPlane(pixels, 1, 1, LinearRec709(100.0));
  const auto calibrationMismatch = Engine::SnapshotAnalysis::ComputeComparison(
      ColorPlane(pixels, 1, 1, LinearRec709(200.0)), differentCalibration, DifferenceMetric::MeanAbsolute);
  ASSERT_FALSE(calibrationMismatch.has_value());
  EXPECT_NE(calibrationMismatch.error().Message.find("color interpretation"), std::string::npos);
}

TEST(SnapshotComparisonTests, ComparesUInt32SamplesExactlyAndRejectsRelativeErrorForThem) {
  const std::array<std::uint32_t, 2> idsA{0U, 4'000'000'000U};
  const std::array<std::uint32_t, 2> idsB{1U, 3'999'999'999U};
  PlaneView a{2, 1, 1, PlaneSemantic::MaterialInstanceId, ScalarFormat::UInt32, {}, {}, idsA};
  PlaneView b{2, 1, 1, PlaneSemantic::MaterialInstanceId, ScalarFormat::UInt32, {}, {}, idsB};
  const auto difference = Engine::SnapshotAnalysis::ComputeComparison(a, b, DifferenceMetric::MeanAbsolute);

  ASSERT_TRUE(difference.has_value()) << difference.error().Message;
  EXPECT_DOUBLE_EQ(difference->ChannelAbsoluteErrors[0], 1.0);
  EXPECT_DOUBLE_EQ(difference->ChannelAbsoluteErrors[1], 1.0);
  EXPECT_DOUBLE_EQ(difference->Statistics.Mean, 1.0);
  EXPECT_FALSE(Engine::SnapshotAnalysis::ComputeComparison(a, b, DifferenceMetric::RelativeLuminance).has_value());
}

TEST(SnapshotComparisonTests, RejectsFloatStorageForIntegerIdentityPlanes) {
  const std::array<float, 1> lossyIds{16'777'216.0F};
  const PlaneView malformed{1, 1, 1, PlaneSemantic::MaterialInstanceId, ScalarFormat::Float32, {}, lossyIds, {}};

  const auto result = Engine::SnapshotAnalysis::ComputeComparison(malformed, malformed, DifferenceMetric::MeanAbsolute);

  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().Message.find("scalar format does not match"), std::string::npos);
}

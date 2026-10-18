#include "panels/include/AnalyticalFrameViewModel.h"

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace {

auto MakeOutput(uint32_t width, uint32_t height, std::vector<float> finalColor) -> Engine::RenderOutput {
  const size_t pixelCount = static_cast<size_t>(width) * height;
  std::vector<Engine::RenderOutputPlane> planes;
  planes.reserve(5);
  for (uint8_t index = 0; index < 5; ++index) {
    const auto aov = static_cast<Engine::RenderAov>(index);
    const uint32_t channels = Engine::RenderOutput::GetDescriptor(aov).ChannelCount;
    auto samples =
        aov == Engine::RenderAov::FinalColor ? std::move(finalColor) : std::vector<float>(pixelCount * channels, 0.0F);
    planes.push_back({.Aov = aov, .ChannelCount = channels, .Pixels = std::move(samples)});
  }
  auto output = Engine::RenderOutput::CreateFromPlanes(width, height, std::move(planes));
  EXPECT_TRUE(output.has_value());
  return output ? std::move(*output) : Engine::RenderOutput{};
}

} // namespace

TEST(AnalyticalFrameViewModelTests, ReadsPinnedPixelChannelsFromTopToBottomPlane) {
  const auto output = MakeOutput(2, 1, {0.1F, 0.2F, 0.3F, 1.0F, 0.4F, 0.5F, 0.6F, 0.75F});

  auto sample = Editor::Panels::AnalyticalFrameViewModel::ReadPixel(output, Engine::RenderAov::FinalColor, 1, 0);

  ASSERT_TRUE(sample.has_value());
  EXPECT_EQ(sample->ChannelCount, 4U);
  EXPECT_FLOAT_EQ(sample->Channels[0], 0.4F);
  EXPECT_FLOAT_EQ(sample->Channels[1], 0.5F);
  EXPECT_FLOAT_EQ(sample->Channels[2], 0.6F);
  EXPECT_FLOAT_EQ(sample->Channels[3], 0.75F);
  EXPECT_FALSE(Editor::Panels::AnalyticalFrameViewModel::ReadPixel(output, Engine::RenderAov::FinalColor, 2, 0));
}

TEST(AnalyticalFrameViewModelTests, ComputesUnclampedRelativeRec709LuminanceError) {
  const auto a = MakeOutput(2, 1, {0.5F, 0.5F, 0.5F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F});
  const auto b = MakeOutput(2, 1, {1.0F, 1.0F, 1.0F, 1.0F, 0.25F, 0.25F, 0.25F, 1.0F});

  auto errors = Editor::Panels::AnalyticalFrameViewModel::ComputeRelativeLuminanceError(a, b);

  ASSERT_TRUE(errors.has_value());
  ASSERT_EQ(errors->size(), 2U);
  EXPECT_NEAR(errors->at(0), 1.0F, 1.0e-6F);
  EXPECT_NEAR(errors->at(1), 250000.0F, 0.1F);
  const auto heatmap = Editor::Panels::AnalyticalFrameViewModel::RelativeErrorHeatmap(errors->at(1));
  EXPECT_EQ(heatmap, (std::array<uint8_t, 4>{255, 255, 0, 255}));
}

TEST(AnalyticalFrameViewModelTests, RejectsDimensionMismatchForLuminanceError) {
  const auto a = MakeOutput(1, 1, {0.5F, 0.5F, 0.5F, 1.0F});
  const auto b = MakeOutput(2, 1, {0.5F, 0.5F, 0.5F, 1.0F, 0.6F, 0.6F, 0.6F, 1.0F});

  EXPECT_FALSE(Editor::Panels::AnalyticalFrameViewModel::HasCompatibleLinearFinalColor(a, b));
  EXPECT_FALSE(Editor::Panels::AnalyticalFrameViewModel::ComputeRelativeLuminanceError(a, b).has_value());
}

TEST(AnalyticalFrameViewModelTests, BuildsStaticWipeFromCapturedBuffers) {
  const auto a = MakeOutput(2, 1, {1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F});
  const auto b = MakeOutput(2, 1, {0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F});
  auto imageA = Editor::Panels::AnalyticalFrameViewModel::BuildDisplayImage(a, Engine::RenderAov::FinalColor);
  auto imageB = Editor::Panels::AnalyticalFrameViewModel::BuildDisplayImage(b, Engine::RenderAov::FinalColor);
  auto wipe = Editor::Panels::AnalyticalFrameViewModel::BuildWipeImage(a, b, Engine::RenderAov::FinalColor, 0.5F);

  ASSERT_TRUE(imageA.has_value());
  ASSERT_TRUE(imageB.has_value());
  ASSERT_TRUE(wipe.has_value());
  const std::array<uint8_t, 4> left{wipe->at(0), wipe->at(1), wipe->at(2), wipe->at(3)};
  const std::array<uint8_t, 4> right{wipe->at(4), wipe->at(5), wipe->at(6), wipe->at(7)};
  const std::array<uint8_t, 4> expectedLeft{imageA->at(0), imageA->at(1), imageA->at(2), imageA->at(3)};
  const std::array<uint8_t, 4> expectedRight{imageB->at(4), imageB->at(5), imageB->at(6), imageB->at(7)};
  EXPECT_EQ(left, expectedLeft);
  EXPECT_EQ(right, expectedRight);
}

TEST(AnalyticalFrameViewModelTests, BuildsDisplayPixelsAtCapturedOutputResolution) {
  const auto output = MakeOutput(3, 2, std::vector<float>(3U * 2U * 4U, 0.25F));

  auto image = Editor::Panels::AnalyticalFrameViewModel::BuildDisplayImage(output, Engine::RenderAov::FinalColor);

  ASSERT_TRUE(image.has_value());
  EXPECT_EQ(output.Width, 3U);
  EXPECT_EQ(output.Height, 2U);
  EXPECT_EQ(image->size(), 3U * 2U * 4U);
}

TEST(AnalyticalFrameViewModelTests, RoundTripsTypedViewportStateWithStableManifestNames) {
  Editor::Panels::AnalyticalFrameViewState state;
  state.SelectedAov = Engine::RenderAov::WorldNormal;
  state.SelectedModel = Engine::Renderer::BrdfModel::AshikhminShirley;
  state.Zoom = 2.5F;
  state.PanOffsetPixels = {12.0F, -3.0F};
  state.PinnedPixel = std::array<uint32_t, 2>{7U, 9U};
  state.CompareModelA = Engine::Renderer::BrdfModel::GGX;
  state.CompareModelB = Engine::Renderer::BrdfModel::Schlick;
  state.Mode = Editor::Panels::AnalyticalFrameViewState::ComparisonMode::Wipe;
  state.WipePosition = 0.25F;

  const auto json = Editor::Panels::AnalyticalFrameViewModel::ToViewportJson(state);
  auto restored = Editor::Panels::AnalyticalFrameViewModel::FromViewportJson(json);

  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->SelectedAov, state.SelectedAov);
  EXPECT_EQ(restored->SelectedModel, state.SelectedModel);
  EXPECT_EQ(restored->Zoom, state.Zoom);
  EXPECT_EQ(restored->PanOffsetPixels, state.PanOffsetPixels);
  EXPECT_EQ(restored->PinnedPixel, state.PinnedPixel);
  EXPECT_EQ(restored->CompareModelA, state.CompareModelA);
  EXPECT_EQ(restored->CompareModelB, state.CompareModelB);
  EXPECT_EQ(restored->Mode, state.Mode);
  EXPECT_EQ(restored->WipePosition, state.WipePosition);
  EXPECT_EQ(json.at("SelectedAov"), "worldNormal");
  EXPECT_EQ(json.at("SelectedModel"), "ashikhminShirley");
  EXPECT_EQ(json.at("Mode"), "Wipe");
  state.SelectedAov = Engine::RenderAov::FinalColor;
  state.Mode = Editor::Panels::AnalyticalFrameViewState::ComparisonMode::RelativeLuminanceError;
  const auto errorJson = Editor::Panels::AnalyticalFrameViewModel::ToViewportJson(state);
  EXPECT_EQ(errorJson.at("SelectedAov"), "finalColor");
  EXPECT_EQ(errorJson.at("Mode"), "RelativeLuminanceError");
}

TEST(AnalyticalFrameViewModelTests, RejectsInvalidViewportState) {
  auto json = Editor::Panels::AnalyticalFrameViewModel::ToViewportJson(Editor::Panels::AnalyticalFrameViewState{});
  json["Mode"] = "futureMode";

  EXPECT_FALSE(Editor::Panels::AnalyticalFrameViewModel::FromViewportJson(json).has_value());
}

TEST(AnalyticalFrameViewModelTests, RejectsRelativeErrorModeWithNonFinalColorAov) {
  auto json = Editor::Panels::AnalyticalFrameViewModel::ToViewportJson(Editor::Panels::AnalyticalFrameViewState{});
  json["SelectedAov"] = "depth";
  json["Mode"] = "RelativeLuminanceError";

  EXPECT_FALSE(Editor::Panels::AnalyticalFrameViewModel::FromViewportJson(json).has_value());
}

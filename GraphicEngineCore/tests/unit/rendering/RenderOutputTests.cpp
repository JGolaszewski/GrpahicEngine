#include "rendering/include/RenderOutput.h"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <utility>
#include <vector>

namespace {

TEST(RenderOutputTests, DescriptorsExposeStableAovNamesAndChannels) {
  constexpr std::array<Engine::RenderAov, 5> aovs = {
      Engine::RenderAov::FinalColor, Engine::RenderAov::Depth, Engine::RenderAov::WorldNormal,
      Engine::RenderAov::SpecularLighting, Engine::RenderAov::DiffuseLighting};
  constexpr std::array<const char*, 5> names = {"final_color", "depth", "world_normal", "specular_lighting",
                                                "diffuse_lighting"};
  constexpr std::array<uint32_t, 5> channelCounts = {4, 1, 3, 3, 3};

  for (size_t index = 0; index < aovs.size(); ++index) {
    const Engine::RenderAovDescriptor& descriptor = Engine::RenderOutput::GetDescriptor(aovs.at(index));
    EXPECT_EQ(descriptor.Aov, aovs.at(index));
    EXPECT_STREQ(descriptor.Name, names.at(index));
    EXPECT_EQ(descriptor.Format, Engine::RenderOutputFormat::Float32);
    EXPECT_EQ(descriptor.ChannelCount, channelCounts.at(index));
    EXPECT_NE(descriptor.ChannelNames[0], nullptr);
  }
}

TEST(RenderOutputTests, ReconstructsAllCanonicalPlanesFromOwnedData) {
  std::vector<Engine::RenderOutputPlane> planes;
  for (uint8_t index = 0; index < 5; ++index) {
    const auto aov = static_cast<Engine::RenderAov>(index);
    const uint32_t channels = Engine::RenderOutput::GetDescriptor(aov).ChannelCount;
    planes.push_back(
        {.Aov = aov, .ChannelCount = channels, .Pixels = std::vector<float>(static_cast<size_t>(2U) * channels, 0.5F)});
  }
  auto output = Engine::RenderOutput::CreateFromPlanes(2, 1, std::move(planes));
  ASSERT_TRUE(output.has_value());
  EXPECT_EQ(output->Width, 2U);
  EXPECT_EQ(output->Height, 1U);
  ASSERT_EQ(output->GetPlanes().size(), 5U);
  EXPECT_EQ(output->GetPixels(Engine::RenderAov::FinalColor).size(), 8U);
  EXPECT_EQ(output->GetPixels(Engine::RenderAov::Depth).size(), 2U);
}

TEST(RenderOutputTests, RejectsIncompleteReorderedWrongLengthAndNonFinitePlaneData) {
  const auto makePlanes = [] {
    std::vector<Engine::RenderOutputPlane> planes;
    for (uint8_t index = 0; index < 5; ++index) {
      const auto aov = static_cast<Engine::RenderAov>(index);
      const uint32_t channels = Engine::RenderOutput::GetDescriptor(aov).ChannelCount;
      planes.push_back({.Aov = aov, .ChannelCount = channels, .Pixels = std::vector<float>(channels, 0.5F)});
    }
    return planes;
  };

  auto incomplete = makePlanes();
  incomplete.pop_back();
  EXPECT_FALSE(Engine::RenderOutput::CreateFromPlanes(1, 1, std::move(incomplete)).has_value());

  auto reordered = makePlanes();
  std::swap(reordered[0], reordered[1]);
  EXPECT_FALSE(Engine::RenderOutput::CreateFromPlanes(1, 1, std::move(reordered)).has_value());

  auto wrongLength = makePlanes();
  wrongLength[0].Pixels.pop_back();
  EXPECT_FALSE(Engine::RenderOutput::CreateFromPlanes(1, 1, std::move(wrongLength)).has_value());

  auto nonFinite = makePlanes();
  nonFinite[2].Pixels[0] = (std::numeric_limits<float>::infinity)();
  EXPECT_FALSE(Engine::RenderOutput::CreateFromPlanes(1, 1, std::move(nonFinite)).has_value());
  EXPECT_FALSE(Engine::RenderOutput::CreateFromPlanes(0, 1, makePlanes()).has_value());
}

} // namespace

#include "rendering/include/RenderOutput.h"

#include <gtest/gtest.h>

#include <array>

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

} // namespace

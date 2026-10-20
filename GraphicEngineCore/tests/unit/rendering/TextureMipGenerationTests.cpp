#include "rendering/src/TextureMipGenerator.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {
auto MakeImage(uint32_t width, uint32_t height, uint8_t value = 0) -> Engine::ImportedImage {
  Engine::ImportedImage image;
  image.width = width;
  image.height = height;
  image.rgba8.resize(static_cast<size_t>(width) * height * 4);
  for (size_t pixel = 0; pixel < static_cast<size_t>(width) * height; ++pixel) {
    image.rgba8[pixel * 4] = value;
    image.rgba8[pixel * 4 + 1] = value;
    image.rgba8[pixel * 4 + 2] = value;
    image.rgba8[pixel * 4 + 3] = 255;
  }
  return image;
}

auto Channel(const Engine::Renderer::TextureMipGenerator::Mip& mip, size_t pixel, size_t channel) -> uint8_t {
  return mip.Pixels.at(pixel * 4 + channel);
}

auto AlphaCoverage(const Engine::Renderer::TextureMipGenerator::Mip& mip, float cutoff) -> float {
  size_t covered = 0;
  for (size_t pixel = 0; pixel < static_cast<size_t>(mip.Width) * mip.Height; ++pixel) {
    covered += static_cast<float>(Channel(mip, pixel, 3)) / 255.0F >= cutoff ? 1U : 0U;
  }
  return static_cast<float>(covered) / static_cast<float>(mip.Width * mip.Height);
}

TEST(TextureMipGenerationTests, OddWidthMipIncludesEverySourceTexel) {
  auto image = MakeImage(3, 1);
  image.rgba8[0] = 30;
  image.rgba8[4] = 90;
  image.rgba8[8] = 240;

  const auto mips = Engine::Renderer::TextureMipGenerator::Build(image, {});

  ASSERT_EQ(mips.size(), 2U);
  EXPECT_EQ(mips.back().Width, 1U);
  EXPECT_EQ(mips.back().Height, 1U);
  EXPECT_EQ(Channel(mips.back(), 0, 0), 120U);
}

TEST(TextureMipGenerationTests, OddNonSquareMipIncludesBottomRightEdgeContribution) {
  auto image = MakeImage(5, 3);
  const size_t bottomRight = (static_cast<size_t>(2) * image.width + 4) * 4;
  image.rgba8[bottomRight] = 255;
  image.rgba8[bottomRight + 1] = 255;
  image.rgba8[bottomRight + 2] = 255;

  const auto mips = Engine::Renderer::TextureMipGenerator::Build(image, {});

  ASSERT_EQ(mips.size(), 3U);
  EXPECT_EQ(mips.at(1).Width, 2U);
  EXPECT_EQ(mips.at(1).Height, 1U);
  EXPECT_EQ(mips.back().Width, 1U);
  EXPECT_EQ(mips.back().Height, 1U);
  EXPECT_NE(Channel(mips.at(1), 1, 0), 0U);
  EXPECT_EQ(Channel(mips.back(), 0, 0), 17U);
}

TEST(TextureMipGenerationTests, InvalidImagesProduceNoMipLevels) {
  Engine::ImportedImage empty;
  EXPECT_TRUE(Engine::Renderer::TextureMipGenerator::Build(empty, {}).empty());

  auto truncated = MakeImage(2, 2);
  truncated.rgba8.pop_back();
  EXPECT_TRUE(Engine::Renderer::TextureMipGenerator::Build(truncated, {}).empty());
}

TEST(TextureMipGenerationTests, SingleTexelImageProducesOnlyItsBaseLevel) {
  const auto mips = Engine::Renderer::TextureMipGenerator::Build(MakeImage(1, 1, 73), {});

  ASSERT_EQ(mips.size(), 1U);
  EXPECT_EQ(mips.front().Width, 1U);
  EXPECT_EQ(mips.front().Height, 1U);
  EXPECT_EQ(Channel(mips.front(), 0, 0), 73U);
}

TEST(TextureMipGenerationTests, SrgbColorIsAveragedInLinearLight) {
  auto image = MakeImage(2, 1);
  image.rgba8[4] = image.rgba8[5] = image.rgba8[6] = 255;

  const auto mips = Engine::Renderer::TextureMipGenerator::Build(image, {.Srgb = true});

  ASSERT_EQ(mips.size(), 2U);
  EXPECT_NEAR(Channel(mips.back(), 0, 0), 188, 1);
}

TEST(TextureMipGenerationTests, NormalMapMipIsRenormalizedAndUsesPositiveZForCancellation) {
  auto image = MakeImage(2, 1);
  image.rgba8[0] = 255; // +X
  image.rgba8[1] = 128;
  image.rgba8[2] = 128;
  image.rgba8[4] = 128; // +Z
  image.rgba8[5] = 128;
  image.rgba8[6] = 255;
  const auto mips = Engine::Renderer::TextureMipGenerator::Build(image, {.NormalMap = true});
  ASSERT_EQ(mips.size(), 2U);
  const float x = static_cast<float>(Channel(mips.back(), 0, 0)) / 127.5F - 1.0F;
  const float y = static_cast<float>(Channel(mips.back(), 0, 1)) / 127.5F - 1.0F;
  const float z = static_cast<float>(Channel(mips.back(), 0, 2)) / 127.5F - 1.0F;
  EXPECT_NEAR(std::sqrt(x * x + y * y + z * z), 1.0F, 0.015F);
  EXPECT_NEAR(x, 0.707F, 0.015F);
  EXPECT_NEAR(z, 0.707F, 0.015F);

  image.rgba8[0] = 255;
  image.rgba8[1] = image.rgba8[2] = 128;
  image.rgba8[4] = 0; // -X
  image.rgba8[5] = image.rgba8[6] = 127;
  const auto cancelling = Engine::Renderer::TextureMipGenerator::Build(image, {.NormalMap = true});
  EXPECT_EQ(Channel(cancelling.back(), 0, 0), 128U);
  EXPECT_EQ(Channel(cancelling.back(), 0, 1), 128U);
  EXPECT_EQ(Channel(cancelling.back(), 0, 2), 255U);
}

TEST(TextureMipGenerationTests, MaskedAlphaMipPreservesNearestRepresentableCoverage) {
  auto image = MakeImage(8, 2);
  for (size_t pixel = 0; pixel < static_cast<size_t>(image.width) * image.height; ++pixel) {
    image.rgba8[pixel * 4 + 3] = 0;
  }
  for (size_t y = 0; y < 2; ++y) {
    for (size_t x = 0; x < 2; ++x) {
      image.rgba8[(y * image.width + x) * 4 + 3] = 255;
    }
  }

  const auto masked = Engine::Renderer::TextureMipGenerator::Build(image, {.AlphaCutoff = 0.5F});

  ASSERT_EQ(masked.size(), 4U);
  EXPECT_EQ(AlphaCoverage(masked.front(), 0.5F), 0.25F);
  EXPECT_FLOAT_EQ(AlphaCoverage(masked.at(1), 0.5F), 0.25F);
  EXPECT_FLOAT_EQ(AlphaCoverage(masked.at(2), 0.5F), 0.5F);
  EXPECT_FLOAT_EQ(AlphaCoverage(masked.at(3), 0.5F), 0.0F);
}
} // namespace

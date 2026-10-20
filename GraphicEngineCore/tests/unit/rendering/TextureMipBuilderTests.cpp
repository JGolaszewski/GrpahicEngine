#include "rendering/src/TextureMipBuilder.h"

#include <gtest/gtest.h>

namespace {
TEST(TextureMipBuilderTests, OddDimensionsIncludeEverySourceTexel) {
  Engine::ImportedImage image;
  image.width = 3;
  image.height = 3;
  image.rgba8.assign(3U * 3U * 4U, 0);
  for (size_t i = 3; i < image.rgba8.size(); i += 4) {
    image.rgba8[i] = 255;
  }
  image.rgba8[8U * 4U] = 255;

  const auto mips = Engine::Renderer::Detail::BuildTextureMips(image, false);
  ASSERT_EQ(mips.size(), 2U);
  EXPECT_EQ(mips[1].Width, 1U);
  EXPECT_EQ(mips[1].Height, 1U);
  EXPECT_EQ(mips[1].Pixels[0], 28U);
}

TEST(TextureMipBuilderTests, NonSquareOddDimensionsCoverTheFullImage) {
  Engine::ImportedImage image;
  image.width = 5;
  image.height = 3;
  image.rgba8.assign(5U * 3U * 4U, 0);
  for (size_t i = 3; i < image.rgba8.size(); i += 4) {
    image.rgba8[i] = 255;
  }
  image.rgba8[(14U * 4U)] = 255;

  const auto mips = Engine::Renderer::Detail::BuildTextureMips(image, false);
  ASSERT_EQ(mips.size(), 3U);
  EXPECT_EQ(mips[1].Width, 2U);
  EXPECT_EQ(mips[1].Height, 1U);
  EXPECT_GT(mips[1].Pixels[4], 0U);
  EXPECT_EQ(mips[2].Pixels[0], 17U);
}

TEST(TextureMipBuilderTests, NormalMapMipTexelsAreRenormalized) {
  Engine::ImportedImage image;
  image.width = image.height = 2;
  image.rgba8 = {255, 128, 128, 255, 0, 128, 128, 255, 128, 128, 255, 255, 128, 128, 255, 255};

  const auto mips = Engine::Renderer::Detail::BuildTextureMips(image, false, true);
  ASSERT_EQ(mips.size(), 2U);
  const float x = static_cast<float>(mips.back().Pixels[0]) / 127.5F - 1.0F;
  const float y = static_cast<float>(mips.back().Pixels[1]) / 127.5F - 1.0F;
  const float z = static_cast<float>(mips.back().Pixels[2]) / 127.5F - 1.0F;
  EXPECT_NEAR(std::sqrt(x * x + y * y + z * z), 1.0F, 0.02F);
}

TEST(TextureMipBuilderTests, MaskedAlphaCoverageIsPreservedWhenMipCanRepresentIt) {
  Engine::ImportedImage image;
  image.width = image.height = 4;
  image.rgba8.resize(4U * 4U * 4U);
  for (uint32_t y = 0; y < 4; ++y) {
    for (uint32_t x = 0; x < 4; ++x) {
      const size_t offset = (static_cast<size_t>(y) * 4U + x) * 4U;
      image.rgba8[offset + 3] = (x + y) % 2 == 0 ? 255 : 0;
    }
  }

  const auto mips = Engine::Renderer::Detail::BuildTextureMips(image, true, false, true);
  ASSERT_EQ(mips.size(), 3U);
  uint32_t covered = 0;
  for (size_t pixel = 3; pixel < mips[1].Pixels.size(); pixel += 4) {
    covered += mips[1].Pixels[pixel] >= 128 ? 1U : 0U;
  }
  EXPECT_EQ(covered, 2U);
}
} // namespace

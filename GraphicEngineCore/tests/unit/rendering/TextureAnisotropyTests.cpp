#include "rendering/include/renderer/Material.h"

#include <gtest/gtest.h>

using Engine::Renderer::ResolveTextureAnisotropy;
using Engine::Renderer::TextureAnisotropy;

TEST(TextureAnisotropyTests, GlobalCapResolvesToEngineMaximum) {
  EXPECT_EQ(ResolveTextureAnisotropy(TextureAnisotropy::GlobalCap, 8), 8U);
  EXPECT_EQ(ResolveTextureAnisotropy(TextureAnisotropy::GlobalCap, 16), 16U);
}

TEST(TextureAnisotropyTests, ExplicitRequestIsClampedToEngineMaximum) {
  EXPECT_EQ(ResolveTextureAnisotropy(TextureAnisotropy::X16, 8), 8U);
  EXPECT_EQ(ResolveTextureAnisotropy(TextureAnisotropy::X4, 8), 4U);
  EXPECT_EQ(ResolveTextureAnisotropy(TextureAnisotropy::X2, 1), 1U);
}

TEST(TextureAnisotropyTests, InvalidValuesFallBackToEngineMaximum) {
  EXPECT_EQ(ResolveTextureAnisotropy(static_cast<TextureAnisotropy>(3), 8), 8U);
  EXPECT_EQ(ResolveTextureAnisotropy(TextureAnisotropy::X4, 3), 4U);
}

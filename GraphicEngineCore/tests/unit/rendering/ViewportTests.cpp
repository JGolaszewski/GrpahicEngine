#include "rendering/include/renderer/Viewport.h"

#include <gtest/gtest.h>

#include <memory>

TEST(ViewportTests, ReturnsStableAspectRatioForValidDimensions)
{
    int framebuffer = 0;
    const Engine::Viewport viewport(640, 480, &framebuffer);

    EXPECT_EQ(viewport.GetWidth(), 640U);
    EXPECT_EQ(viewport.GetHeight(), 480U);
    EXPECT_EQ(viewport.GetFramebufferHandle(), &framebuffer);
    EXPECT_FLOAT_EQ(viewport.GetAspectRatio(), 4.0f / 3.0f);
}

TEST(ViewportTests, UsesUnitAspectRatioWhenEitherDimensionIsZero)
{
    const Engine::Viewport zeroWidth(0, 480, nullptr);
    const Engine::Viewport zeroHeight(640, 0, nullptr);
    const Engine::Viewport missingTarget(std::shared_ptr<Engine::ViewportTarget>{});

    EXPECT_FLOAT_EQ(zeroWidth.GetAspectRatio(), 1.0f);
    EXPECT_FLOAT_EQ(zeroHeight.GetAspectRatio(), 1.0f);
    EXPECT_EQ(missingTarget.GetWidth(), 0U);
    EXPECT_EQ(missingTarget.GetHeight(), 0U);
    EXPECT_EQ(missingTarget.GetFramebufferHandle(), nullptr);
    EXPECT_FLOAT_EQ(missingTarget.GetAspectRatio(), 1.0f);
}

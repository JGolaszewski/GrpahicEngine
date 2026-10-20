#include "panels/include/CameraPreviewImageLayout.h"

#include <gtest/gtest.h>

TEST(CameraPreviewImageLayoutTests, FitsImageWithoutChangingItsAspectRatio) {
  const auto image = CameraPreviewImageLayout::ComputeImageSize(800, 400, 400.0F, 300.0F, 1.0F);

  EXPECT_FLOAT_EQ(image.FitScale, 0.5F);
  EXPECT_FLOAT_EQ(image.Width, 400.0F);
  EXPECT_FLOAT_EQ(image.Height, 200.0F);
  EXPECT_FLOAT_EQ(image.ScrollMaxX, 0.0F);
  EXPECT_FLOAT_EQ(image.ScrollMaxY, 0.0F);
}

TEST(CameraPreviewImageLayoutTests, ZoomCreatesPanRangeForAnImageLargerThanTheViewer) {
  const auto image = CameraPreviewImageLayout::ComputeImageSize(800, 400, 400.0F, 300.0F, 2.0F);

  EXPECT_FLOAT_EQ(image.Width, 800.0F);
  EXPECT_FLOAT_EQ(image.Height, 400.0F);
  EXPECT_FLOAT_EQ(image.ScrollMaxX, 400.0F);
  EXPECT_FLOAT_EQ(image.ScrollMaxY, 100.0F);
}

TEST(CameraPreviewImageLayoutTests, InvalidImageOrViewportHasNoDisplayScale) {
  EXPECT_FLOAT_EQ(CameraPreviewImageLayout::ComputeFitScale(0, 400, 400.0F, 300.0F), 0.0F);
  EXPECT_FLOAT_EQ(CameraPreviewImageLayout::ComputeFitScale(800, 400, 0.0F, 300.0F), 0.0F);
  EXPECT_FLOAT_EQ(CameraPreviewImageLayout::ClampZoom(-1.0F), 0.05F);
  EXPECT_FLOAT_EQ(CameraPreviewImageLayout::ClampZoom(100.0F), 32.0F);
}

TEST(CameraPreviewImageLayoutTests, ResizingClampsPanAndRecentersWhenImageFits) {
  const auto narrow = CameraPreviewImageLayout::ComputeImageSize(800, 400, 300.0F, 200.0F, 2.0F);
  ASSERT_FLOAT_EQ(narrow.ScrollMaxX, 300.0F);
  ASSERT_FLOAT_EQ(narrow.ScrollMaxY, 100.0F);
  EXPECT_FLOAT_EQ(CameraPreviewImageLayout::ClampPanOffset(-700.0F, narrow.ScrollMaxX), -300.0F);
  EXPECT_FLOAT_EQ(CameraPreviewImageLayout::ClampPanOffset(40.0F, narrow.ScrollMaxX), 0.0F);

  const auto resized = CameraPreviewImageLayout::ComputeImageSize(800, 400, 1000.0F, 700.0F, 1.0F);
  EXPECT_FLOAT_EQ(resized.ScrollMaxX, 0.0F);
  EXPECT_FLOAT_EQ(resized.ScrollMaxY, 0.0F);
  EXPECT_FLOAT_EQ(CameraPreviewImageLayout::ClampPanOffset(-120.0F, resized.ScrollMaxX), 0.0F);
  EXPECT_FLOAT_EQ(CameraPreviewImageLayout::ClampPanOffset(40.0F, resized.ScrollMaxY), 0.0F);
}

TEST(CameraPreviewImageLayoutTests, PanStartsOnlyOnTheImageAndContinuesUntilReleaseOutsideIt) {
  CameraPreviewImageLayout::PanState state;

  state = CameraPreviewImageLayout::UpdatePan(state, true, false, false, true, 8.0F, -3.0F);
  EXPECT_FALSE(state.IsPanning);
  EXPECT_FLOAT_EQ(state.OffsetX, 0.0F);
  EXPECT_FLOAT_EQ(state.OffsetY, 0.0F);

  state = CameraPreviewImageLayout::UpdatePan(state, true, false, true, false, 0.0F, 0.0F);
  ASSERT_TRUE(state.IsPanning);
  state = CameraPreviewImageLayout::UpdatePan(state, false, false, false, true, 8.0F, -3.0F);
  EXPECT_TRUE(state.IsPanning);
  EXPECT_FLOAT_EQ(state.OffsetX, 8.0F);
  EXPECT_FLOAT_EQ(state.OffsetY, -3.0F);

  state = CameraPreviewImageLayout::UpdatePan(state, false, true, false, false, 0.0F, 0.0F);
  EXPECT_FALSE(state.IsPanning);
}

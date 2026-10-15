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

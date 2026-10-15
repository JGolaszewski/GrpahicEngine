#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

/// Geometry helpers used by the full-resolution camera image viewer.
namespace CameraPreviewImageLayout {

struct ImageSize {
  float Width = 0.0F;
  float Height = 0.0F;
  float FitScale = 0.0F;
  float ScrollMaxX = 0.0F;
  float ScrollMaxY = 0.0F;
};

/// Returns the aspect-preserving scale that fits an image in the available area.
inline auto ComputeFitScale(uint32_t imageWidth, uint32_t imageHeight, float availableWidth,
                            float availableHeight) noexcept -> float {
  if (imageWidth == 0 || imageHeight == 0 || !std::isfinite(availableWidth) || !std::isfinite(availableHeight) ||
      availableWidth <= 0.0F || availableHeight <= 0.0F) {
    return 0.0F;
  }
  return (std::min)(availableWidth / static_cast<float>(imageWidth), availableHeight / static_cast<float>(imageHeight));
}

/// Clamps the user zoom multiplier relative to the current fit scale.
inline auto ClampZoom(float zoom) noexcept -> float {
  if (!std::isfinite(zoom)) {
    return 1.0F;
  }
  return std::clamp(zoom, 0.05F, 32.0F);
}

/// Computes the visible image size and the scroll range used to pan a zoomed image.
inline auto ComputeImageSize(uint32_t imageWidth, uint32_t imageHeight, float availableWidth, float availableHeight,
                             float zoom) noexcept -> ImageSize {
  ImageSize result;
  result.FitScale = ComputeFitScale(imageWidth, imageHeight, availableWidth, availableHeight);
  if (result.FitScale <= 0.0F) {
    return result;
  }
  const float scale = result.FitScale * ClampZoom(zoom);
  result.Width = static_cast<float>(imageWidth) * scale;
  result.Height = static_cast<float>(imageHeight) * scale;
  result.ScrollMaxX = (std::max)(result.Width - availableWidth, 0.0F);
  result.ScrollMaxY = (std::max)(result.Height - availableHeight, 0.0F);
  return result;
}

} // namespace CameraPreviewImageLayout

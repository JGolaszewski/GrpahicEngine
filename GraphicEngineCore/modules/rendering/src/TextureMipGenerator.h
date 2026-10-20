#pragma once

#include "assets/include/AssetImporter.h"

#include <cstdint>
#include <vector>

namespace Engine::Renderer::TextureMipGenerator {
struct Mip {
  uint32_t Width = 0;
  uint32_t Height = 0;
  std::vector<uint8_t> Pixels;
};

struct Options {
  /// Filter RGB as sRGB encoded color, converting to linear light and back.
  bool Srgb = false;
  /// Filter RGB as signed normal vectors and renormalize each generated level.
  bool NormalMap = false;
  /// Preserve base-image alpha coverage at this cutoff; negative disables coverage adjustment.
  float AlphaCutoff = -1.0F;
};

/// Builds a complete mip chain using area-weighted filtering. A non-negative alpha cutoff
/// preserves the source image's binary alpha coverage in every generated level.
auto Build(const Engine::ImportedImage& image, Options options) -> std::vector<Mip>;
} // namespace Engine::Renderer::TextureMipGenerator

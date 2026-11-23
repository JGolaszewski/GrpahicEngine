#pragma once

#include "assets/include/AssetImporter.h"
#include "rendering/include/TextureMipSemantic.h"

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
  /// Channel interpretation used for RGB filtering and optional alpha coverage.
  Engine::Renderer::TextureMipSemantic Semantic = Engine::Renderer::TextureMipSemantic::BaseColor;
  /// Alpha threshold used by MaskedBaseColor; ignored for every other semantic.
  float AlphaCutoff = 0.5F;
};

/// Builds a complete mip chain using area-weighted filtering. A non-negative alpha cutoff
/// preserves masked base-color coverage in every generated level.
auto Build(const Engine::ImportedImage& image, Options options) -> std::vector<Mip>;
} // namespace Engine::Renderer::TextureMipGenerator

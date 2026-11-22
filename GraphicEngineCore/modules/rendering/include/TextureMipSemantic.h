#pragma once

namespace Engine::Renderer {
/// Describes how RGBA channels are interpreted while generating a texture mip chain.
enum class TextureMipSemantic {
  /// Base color RGB is filtered with alpha weighting; alpha stores opacity.
  BaseColor,
  /// Masked base color, with alpha coverage adjusted at the material cutoff.
  MaskedBaseColor,
  /// Color RGB is sRGB-aware when requested, but alpha does not weight RGB.
  Color,
  /// Material data channels are filtered independently; alpha does not weight RGB.
  Data,
  /// RGB stores a tangent-space normal; generated RGB vectors are normalized.
  NormalMap,
};
} // namespace Engine::Renderer

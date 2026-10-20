#pragma once

#include "rendering/include/RenderOutput.h"
#include "runtime/include/Result.h"

#include <cstdint>
#include <filesystem>
#include <span>

namespace Engine {

/// Encodes CPU-owned final-render outputs as PNG or multipart OpenEXR files and writes display-ready RGBA8 PNGs.
class ENGINE_API RenderOutputExporter {
public:
  /// Writes final_color as an 8-bit RGBA PNG. Linear scene-referred RGB is
  /// mapped with Reinhard tone mapping followed by the sRGB transfer function;
  /// alpha is clamped and stored without a transfer function. An enabled
  /// background composites in linear RGB and makes final_color opaque.
  /// @param output Completed render output containing the final_color plane.
  /// @param path Destination PNG path.
  /// @param background Optional per-export solid background.
  /// @return Success or a render-output, path, or encoder error.
  static Status WritePng(const RenderOutput& output, const std::filesystem::path& path,
                         const RenderOutputBackground& background = {});

  /// Writes display-ready interleaved RGBA8 pixels without changing their values.
  /// @param width Image width in pixels.
  /// @param height Image height in pixels.
  /// @param rgba Interleaved red, green, blue, alpha bytes, with four bytes per pixel.
  /// @param path Destination PNG path.
  /// @return Success or a dimension, pixel-buffer, path, or encoder error.
  static Status WritePngRgba8(uint32_t width, uint32_t height, std::span<const uint8_t> rgba,
                              const std::filesystem::path& path);

  /// Writes each selected AOV as a named multipart OpenEXR part, preserving
  /// float32 samples and the stable channel names in its descriptor.
  /// @param output Completed render output containing the selected AOVs.
  /// @param selectedAovs AOVs to write, in the requested part order.
  /// @param path Destination OpenEXR path.
  /// @param background Optional per-export solid background applied only to final_color.
  /// @return Success or a render-output, path, or encoder error.
  static Status WriteMultilayerExr(const RenderOutput& output, std::span<const RenderAov> selectedAovs,
                                   const std::filesystem::path& path, const RenderOutputBackground& background = {});
};

} // namespace Engine

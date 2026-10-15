#pragma once

#include "rendering/include/RenderOutput.h"
#include "runtime/include/Result.h"

#include <filesystem>
#include <span>

namespace Engine {

/// Encodes CPU-owned final-render outputs as PNG or multipart OpenEXR files.
class ENGINE_API RenderOutputExporter {
public:
  /// Writes final_color as an 8-bit RGBA PNG. Linear scene-referred RGB is
  /// mapped with Reinhard tone mapping followed by the sRGB transfer function;
  /// alpha is clamped and stored without a transfer function.
  /// @param output Completed render output containing the final_color plane.
  /// @param path Destination PNG path.
  /// @return Success or a render-output, path, or encoder error.
  static Status WritePng(const RenderOutput& output, const std::filesystem::path& path);

  /// Writes each selected AOV as a named multipart OpenEXR part, preserving
  /// float32 samples and the stable channel names in its descriptor.
  /// @param output Completed render output containing the selected AOVs.
  /// @param selectedAovs AOVs to write, in the requested part order.
  /// @param path Destination OpenEXR path.
  /// @return Success or a render-output, path, or encoder error.
  static Status WriteMultilayerExr(const RenderOutput& output, std::span<const RenderAov> selectedAovs,
                                   const std::filesystem::path& path);
};

} // namespace Engine

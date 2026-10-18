#pragma once

#include "panels/include/AnalyticalFrameViewState.h"
#include "rendering/include/RenderOutput.h"
#include "runtime/include/Result.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace AnalyticalFrame::AnalyticalFrameViewModel {

/// Encodes the typed viewer state using the stable strings used by `.aframe` manifests.
[[nodiscard]] auto ToViewportJson(const AnalyticalFrameViewState& state) -> nlohmann::json;

/// Decodes and validates persisted viewer state.
[[nodiscard]] auto FromViewportJson(const nlohmann::json& value) -> Engine::Result<AnalyticalFrameViewState>;

/// Raw channel values read from one captured pixel.
struct PixelSample {
  uint32_t ChannelCount = 0;
  std::array<float, 4> Channels{};
};

/// Reads one top-to-bottom pixel from a captured AOV plane.
/// @param output CPU-owned frame output.
/// @param aov AOV plane to inspect.
/// @param x Pixel column.
/// @param y Pixel row, measured from the top of the image.
/// @return The raw channel values, or empty for an invalid coordinate or plane.
[[nodiscard]] auto ReadPixel(const Engine::RenderOutput& output, Engine::RenderAov aov, uint32_t x, uint32_t y) noexcept
    -> std::optional<PixelSample>;

/// Converts a captured plane to top-to-bottom display-ready RGBA8 pixels.
/// Final color and lighting use a simple Reinhard tone map and sRGB encoding;
/// normals are remapped from [-1, 1] and depth is shown as grayscale.
[[nodiscard]] auto BuildDisplayImage(const Engine::RenderOutput& output, Engine::RenderAov aov)
    -> Engine::Result<std::vector<uint8_t>>;

/// Checks whether two captured final-color buffers can be compared as linear RGB.
[[nodiscard]] auto HasCompatibleLinearFinalColor(const Engine::RenderOutput& a, const Engine::RenderOutput& b) noexcept
    -> bool;

/// Computes unclamped relative Rec. 709 luminance error per pixel.
/// The result is `|YB - YA| / max(|YA|, 1e-6)` and remains linear/unclamped.
/// Display code should clamp values to [0, 1] independently.
[[nodiscard]] auto ComputeRelativeLuminanceError(const Engine::RenderOutput& a, const Engine::RenderOutput& b)
    -> Engine::Result<std::vector<float>>;

/// Builds a display-ready A/B wipe image from compatible AOV planes.
[[nodiscard]] auto BuildWipeImage(const Engine::RenderOutput& a, const Engine::RenderOutput& b, Engine::RenderAov aov,
                                  float wipePosition) -> Engine::Result<std::vector<uint8_t>>;

/// Maps an unclamped relative error to a red/yellow/white heatmap pixel.
[[nodiscard]] auto RelativeErrorHeatmap(float error) noexcept -> std::array<uint8_t, 4>;

/// Builds the clamped display heatmap for compatible linear final-color buffers.
[[nodiscard]] auto BuildRelativeLuminanceHeatmap(const Engine::RenderOutput& a, const Engine::RenderOutput& b)
    -> Engine::Result<std::vector<uint8_t>>;

} // namespace AnalyticalFrame::AnalyticalFrameViewModel

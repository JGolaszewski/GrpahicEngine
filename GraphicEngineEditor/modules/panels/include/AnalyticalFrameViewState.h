#pragma once

#include "rendering/include/RenderOutput.h"
#include "rendering/include/renderer/Material.h"

#include <array>
#include <cstdint>
#include <optional>

namespace AnalyticalFrame {

/// View-only selection and navigation state for a captured Analytical Frame.
/// This state is persisted with a project and never causes scene rendering.
struct AnalyticalFrameViewState {
  enum class ComparisonMode : uint8_t {
    Single,
    Wipe,
    RelativeLuminanceError,
  };

  Engine::RenderAov SelectedAov = Engine::RenderAov::FinalColor;                   ///< AOV shown by the static viewer.
  Engine::Renderer::BrdfModel SelectedModel = Engine::Renderer::BrdfModel::Legacy; ///< Active model in single view.
  float Zoom = 1.0F;                                  ///< Multiplier applied to the view's fit-to-window scale.
  std::array<float, 2> PanOffsetPixels{};             ///< Display-space pan offset from the fitted image origin.
  std::optional<std::array<uint32_t, 2>> PinnedPixel; ///< Captured-image pixel retained for inspection.
  Engine::Renderer::BrdfModel CompareModelA = Engine::Renderer::BrdfModel::Legacy; ///< Left side of the A/B view.
  Engine::Renderer::BrdfModel CompareModelB = Engine::Renderer::BrdfModel::GGX;    ///< Right side of the A/B view.
  ComparisonMode Mode = ComparisonMode::Single; ///< Single, wipe, or compatible linear-color error display.
  float WipePosition = 0.5F;                    ///< Normalized A/B wipe location in [0, 1].
};

} // namespace AnalyticalFrame

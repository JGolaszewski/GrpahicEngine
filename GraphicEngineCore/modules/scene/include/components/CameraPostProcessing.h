#pragma once

#include "runtime/include/Result.h"

#include <cmath>
#include <optional>
#include <variant>
#include <vector>

namespace Engine::Ecs {

/// Linear color grading controls applied to an authored camera's final color.
struct ColorGradingEffect {
  float Exposure = 0.0F;   ///< Exposure adjustment in stops.
  float Contrast = 1.0F;   ///< Contrast multiplier around 18% linear gray.
  float Saturation = 1.0F; ///< Saturation multiplier.
};

/// Thresholded blurred highlight contribution for an authored camera.
struct BloomEffect {
  float Threshold = 1.0F;  ///< Linear luminance threshold for bright pixels.
  float Intensity = 0.25F; ///< Additive glow strength.
  float Radius = 4.0F;     ///< Approximate screen-space blur radius in pixels.
};

/// Camera-scoped screen-space occlusion applied to indirect diffuse lighting before forward shading.
struct ScreenSpaceAmbientOcclusionSettings {
  float Radius = 0.5F;    ///< World-space sample radius in scene units.
  float Intensity = 1.0F; ///< Occlusion strength from zero to four.
};

/// One configured post-processing stage in a camera's ordered stack.
using CameraPostProcessEffect = std::variant<ColorGradingEffect, BloomEffect>;

/// Optional GPU post-processing configuration attached to a camera entity.
struct CameraPostProcessing {
  std::vector<CameraPostProcessEffect> Effects; ///< Effects in processing order; empty means pass-through.
  std::optional<ScreenSpaceAmbientOcclusionSettings> AmbientOcclusion; ///< Lighting-stage SSAO; absent means disabled.
};

/// Returns whether the camera has nonzero screen-space ambient occlusion work to execute.
/// @param component Camera post-processing configuration.
/// @return True when SSAO is enabled with positive radius and intensity.
inline auto HasScreenSpaceAmbientOcclusion(const CameraPostProcessing& component) noexcept -> bool {
  return component.AmbientOcclusion && component.AmbientOcclusion->Radius > 0.0F &&
         component.AmbientOcclusion->Intensity > 0.0F;
}

/// Validates effect values before they enter a scene or renderer.
/// @param component Camera post-processing configuration.
/// @return Success, or a diagnostic for the first invalid effect value.
inline auto Validate(const CameraPostProcessing& component) -> Status {
  if (component.AmbientOcclusion &&
      (!std::isfinite(component.AmbientOcclusion->Radius) || component.AmbientOcclusion->Radius < 0.0F ||
       component.AmbientOcclusion->Radius > 5.0F || !std::isfinite(component.AmbientOcclusion->Intensity) ||
       component.AmbientOcclusion->Intensity < 0.0F || component.AmbientOcclusion->Intensity > 4.0F)) {
    return std::unexpected(MakeError("Screen-space ambient occlusion values are outside supported ranges"));
  }
  for (const CameraPostProcessEffect& effect : component.Effects) {
    if (const auto* grading = std::get_if<ColorGradingEffect>(&effect)) {
      if (!std::isfinite(grading->Exposure) || grading->Exposure < -16.0F || grading->Exposure > 16.0F ||
          !std::isfinite(grading->Contrast) || grading->Contrast < 0.0F || grading->Contrast > 4.0F ||
          !std::isfinite(grading->Saturation) || grading->Saturation < 0.0F || grading->Saturation > 4.0F) {
        return std::unexpected(MakeError("Color grading values are outside supported ranges"));
      }
    } else if (const auto* bloom = std::get_if<BloomEffect>(&effect)) {
      if (!std::isfinite(bloom->Threshold) || bloom->Threshold < 0.0F || bloom->Threshold > 100000.0F ||
          !std::isfinite(bloom->Intensity) || bloom->Intensity < 0.0F || bloom->Intensity > 10.0F ||
          !std::isfinite(bloom->Radius) || bloom->Radius < 0.0F || bloom->Radius > 64.0F) {
        return std::unexpected(MakeError("Bloom values are outside supported ranges"));
      }
    }
  }
  return {};
}

} // namespace Engine::Ecs

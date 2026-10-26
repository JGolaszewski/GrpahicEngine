#pragma once

#include "runtime/include/Result.h"

#include <array>
#include <cmath>

namespace Engine {

/// Diffuse ambient fill shared by scene views unless an authored camera overrides it.
struct AmbientLightSettings {
  bool Enabled = true;                             ///< Whether ambient diffuse fill contributes to shading.
  std::array<float, 3> Color = {1.0F, 1.0F, 1.0F}; ///< Linear RGB ambient tint.
  float Intensity = 0.15F;                         ///< Nonnegative ambient diffuse multiplier.

  auto operator==(const AmbientLightSettings& other) const noexcept -> bool = default;
};

/// Validates scene or camera ambient-light settings.
/// @param settings Ambient-light configuration to validate.
/// @return Success, or a diagnostic for an unsupported value.
inline auto Validate(const AmbientLightSettings& settings) -> Status {
  if (!std::isfinite(settings.Intensity) || settings.Intensity < 0.0F || settings.Intensity > 10.0F) {
    return std::unexpected(MakeError("Ambient light intensity must be finite and in [0, 10]"));
  }
  for (const float channel : settings.Color) {
    if (!std::isfinite(channel) || channel < 0.0F || channel > 1.0F) {
      return std::unexpected(MakeError("Ambient light color channels must be finite and in [0, 1]"));
    }
  }
  return {};
}

} // namespace Engine

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Engine::Renderer::Brdf {
inline constexpr float MinSawickiAlpha = 1.0e-3f;

/// Converts Beckmann slope roughness to Sawicki's N_DS coefficient by matching the distributions at their half-height
/// angle using Sawicki (2021), equations (9) and (10). Input is clamped to the supported Beckmann parameter range.
inline float SawickiNdsFromBeckmannAlpha(float alpha) noexcept {
  const double beckmannAlpha =
      std::isfinite(alpha) ? std::clamp(static_cast<double>(alpha), static_cast<double>(MinSawickiAlpha), 1.0) : 0.5;

  // Equation (9) is monotonic in cos(beta_half): it tends to infinity at zero and zero at one.
  double lowerCosine = 1.0e-12;
  double upperCosine = 1.0 - 1.0e-12;
  for (uint32_t iteration = 0; iteration < 64; ++iteration) {
    const double cosine = 0.5 * (lowerCosine + upperCosine);
    const double tangent = std::sqrt((std::max)(1.0 - cosine * cosine, 0.0)) / cosine;
    const double denominator = std::sqrt(std::log(2.0) - 4.0 * std::log(cosine));
    const double alphaAtCosine = tangent / denominator;
    if (alphaAtCosine > beckmannAlpha) {
      lowerCosine = cosine;
    } else {
      upperCosine = cosine;
    }
  }

  const double halfAngleCosine = 0.5 * (lowerCosine + upperCosine);
  const double numerator = halfAngleCosine + 3.0 - 4.0 * std::pow(2.0 * halfAngleCosine, 0.25);
  return static_cast<float>(numerator / (halfAngleCosine - 1.0));
}
} // namespace Engine::Renderer::Brdf

#pragma once

#include <array>
#include <cmath>
#include <numbers>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Engine::Runtime::Detail {

/// Encodes PRISM's seven roughness, anisotropy, and axial-orientation controls.
inline auto ParsePrismControlFeatures(std::string_view prompt, bool directional) -> std::array<float, 7> {
  const std::string text(prompt);
  constexpr std::string_view number = R"(([-+]?(?:\d+(?:\.\d*)?|\.\d+)))";
  std::array<float, 7> features{};
  const auto parseUnitControl = [&text, &features, number](std::string_view label, size_t featureIndex) {
    const std::regex pattern("\\b" + std::string(label) + "\\s*(?:=|:)\\s*" + std::string(number) +
                             "\\s*(%|percent)?(?=\\s|[,;.]|$)",
                             std::regex::icase);
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) {
      return;
    }
    float value = std::stof(match[1].str());
    if (match[2].matched) {
      value /= 100.0F;
    }
    if (!std::isfinite(value) || value < 0.0F || value > 1.0F) {
      throw std::runtime_error(std::string(label) + " control must be between 0 and 1 (or 0% and 100%)");
    }
    features[featureIndex] = value;
    features[featureIndex + 4] = 1.0F;
  };
  parseUnitControl("roughness", 0);
  parseUnitControl("anisotropy", 1);
  // Map the editor's binary toggle to the new predictor's numeric feature.
  if (directional && features[5] == 0.0F) {
    features[1] = 1.0F;
    features[5] = 1.0F;
  }

  const std::regex anglePattern("\\b(?:orientation|angle)\\s*(?:=|:)\\s*" + std::string(number) +
                                    "\\s*(?:degrees?|deg|°)?(?=\\s|[,;.]|$)",
                                std::regex::icase);
  std::smatch angleMatch;
  if (std::regex_search(text, angleMatch, anglePattern)) {
    const float degrees = std::stof(angleMatch[1].str());
    if (!std::isfinite(degrees)) {
      throw std::runtime_error("orientation control must be a finite angle in degrees");
    }
    const float radians = std::fmod(degrees, 180.0F) * (std::numbers::pi_v<float> / 180.0F);
    features[2] = std::sin(2.0F * radians);
    features[3] = std::cos(2.0F * radians);
    features[6] = 1.0F;
  }
  return features;
}

} // namespace Engine::Runtime::Detail

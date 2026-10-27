#pragma once

#include <array>
#include <cstdint>

namespace Engine {

/// Versioned PRISM NDF payload shared by serialized materials and the renderer.
inline constexpr uint32_t PrismNdfGridSize = 64;
inline constexpr uint32_t PrismNdfDegree = 8;
inline constexpr uint32_t PrismNdfCoefficientCount = 45;
inline constexpr uint32_t PrismNdfSampleCount = PrismNdfGridSize * PrismNdfGridSize;
inline constexpr uint32_t PrismNdfDataVersion = 1;

/// CPU render payload. Residuals are signed row-major samples over the unit disk.
struct PrismNdfData {
  std::array<float, PrismNdfCoefficientCount> zernikeCoefficients{};
  std::array<float, PrismNdfSampleCount> residuals{};
  float normalization = 1.0F;
  auto operator==(const PrismNdfData& other) const noexcept -> bool = default;
};

} // namespace Engine

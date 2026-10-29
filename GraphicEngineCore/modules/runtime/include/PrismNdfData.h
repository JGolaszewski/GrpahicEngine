#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace Engine {

/// Version 2 PRISM NDF payload shared by serialized materials and the renderer.
/// Residual samples use a 128x128 row-major grid; scene loading migrates version 1 64x64 payloads.
inline constexpr uint32_t PrismNdfGridSize = 128;
inline constexpr uint32_t PrismNdfDegree = 8;
inline constexpr uint32_t PrismNdfCoefficientCount = 45;
inline constexpr uint32_t PrismNdfSampleCount = PrismNdfGridSize * PrismNdfGridSize;
inline constexpr uint32_t PrismNdfDataVersion = 2;

enum class PrismNdfResidualFilter : uint8_t { Bilinear = 0, QuinticUv = 1 };

/// CPU render payload. Residuals are heap-backed signed row-major samples over the unit disk.
/// A valid payload contains exactly PrismNdfSampleCount residuals; the default payload is empty.
struct PrismNdfData {
  std::array<float, PrismNdfCoefficientCount> zernikeCoefficients{};
  std::vector<float> residuals;
  float normalization = 1.0F;
  bool residualsEnabled = true; ///< Whether rendering adds the signed residual grid to the coefficient basis.
  PrismNdfResidualFilter residualFilter = PrismNdfResidualFilter::Bilinear;
  auto operator==(const PrismNdfData& other) const noexcept -> bool = default;
};

} // namespace Engine

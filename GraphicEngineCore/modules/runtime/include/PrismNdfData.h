#pragma once

#include <array>
#include <cstdint>

namespace Engine {

/// Version 2 PRISM NDF payload shared by serialized materials and the renderer.
/// Residual samples use a 128x128 row-major grid; scene loading migrates version 1 64x64 payloads.
inline constexpr uint32_t PrismNdfGridSize = 128;
inline constexpr uint32_t PrismNdfDegree = 8;
inline constexpr uint32_t PrismNdfCoefficientCount = 45;
inline constexpr uint32_t PrismNdfSampleCount = PrismNdfGridSize * PrismNdfGridSize;
inline constexpr uint32_t PrismNdfDataVersion = 2;

/// CPU render payload. Residuals are signed row-major samples over the unit disk.
struct PrismNdfData {
  std::array<float, PrismNdfCoefficientCount> zernikeCoefficients{};
  std::array<float, PrismNdfSampleCount> residuals{};
  float normalization = 1.0F;
  auto operator==(const PrismNdfData& other) const noexcept -> bool = default;
};

} // namespace Engine

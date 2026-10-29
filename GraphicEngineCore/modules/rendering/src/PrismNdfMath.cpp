#include "rendering/include/renderer/PrismNdfMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace Engine::Renderer {
namespace {
constexpr uint32_t RadialQuadratureResolution = 1024;
constexpr uint32_t AngularQuadratureResolution = 2048;
using BasisVector = std::array<double, PrismNdfCoefficientCount>;
using TriangularMatrix = std::array<BasisVector, PrismNdfCoefficientCount>;

auto Jacobi(uint32_t order, double alpha, double beta, double x) noexcept -> double {
  if (order == 0) {
    return 1.0;
  }
  double previousPrevious = 1.0;
  double previous = 0.5 * (alpha - beta + (alpha + beta + 2.0) * x);
  for (uint32_t index = 2; index <= order; ++index) {
    const double k = static_cast<double>(index);
    const double twoKAlphaBeta = 2.0 * k + alpha + beta;
    const double denominator = 2.0 * k * (k + alpha + beta) * (twoKAlphaBeta - 2.0);
    const double first =
        (twoKAlphaBeta - 1.0) * (twoKAlphaBeta * (twoKAlphaBeta - 2.0) * x + alpha * alpha - beta * beta) * previous;
    const double second = 2.0 * (k + alpha - 1.0) * (k + beta - 1.0) * twoKAlphaBeta * previousPrevious;
    const double current = (first - second) / denominator;
    previousPrevious = previous;
    previous = current;
  }
  return previous;
}

auto EvaluateBasis(uint32_t n, int32_t m, double x, double y) noexcept -> double {
  const uint32_t absoluteM = static_cast<uint32_t>(std::abs(m));
  const double radiusSquared = x * x + y * y;
  if (radiusSquared > 1.0 || absoluteM > n || ((n - absoluteM) & 1U) != 0U) {
    return 0.0;
  }
  const double radial = Jacobi((n - absoluteM) / 2U, 0.0, static_cast<double>(absoluteM), 2.0 * radiusSquared - 1.0);
  double real = 1.0;
  double imaginary = 0.0;
  for (uint32_t power = 0; power < absoluteM; ++power) {
    const double nextReal = real * x - imaginary * y;
    imaginary = real * y + imaginary * x;
    real = nextReal;
  }
  return radial * (m < 0 ? imaginary : real);
}

auto BasisAt(double x, double y) noexcept -> BasisVector {
  BasisVector basis{};
  size_t index = 0;
  for (uint32_t n = 0; n <= PrismNdfDegree; ++n) {
    for (int32_t m = -static_cast<int32_t>(n); m <= static_cast<int32_t>(n); m += 2) {
      basis[index++] = EvaluateBasis(n, m, x, y);
    }
  }
  return basis;
}

auto IsFiniteGrid(std::span<const float> grid, std::string& error) -> bool {
  if (grid.size() != PrismNdfSampleCount) {
    error = "PRISM NDF grid must contain exactly " + std::to_string(PrismNdfSampleCount) + " samples";
    return false;
  }
  if (!std::ranges::all_of(grid, [](float value) { return std::isfinite(value) && value >= 0.0F; })) {
    error = "PRISM NDF grid must contain only finite non-negative values";
    return false;
  }
  return true;
}

auto AccumulateQrRow(TriangularMatrix& upper, BasisVector& transformedTarget, BasisVector row, double target) noexcept
    -> void {
  for (size_t column = 0; column < PrismNdfCoefficientCount; ++column) {
    const double diagonal = upper[column][column];
    const double incoming = row[column];
    if (incoming == 0.0) {
      continue;
    }
    const double radius = std::hypot(diagonal, incoming);
    const double cosine = diagonal / radius;
    const double sine = incoming / radius;
    for (size_t entry = column; entry < PrismNdfCoefficientCount; ++entry) {
      const double existing = upper[column][entry];
      upper[column][entry] = cosine * existing + sine * row[entry];
      row[entry] = -sine * existing + cosine * row[entry];
    }
    const double existingTarget = transformedTarget[column];
    transformedTarget[column] = cosine * existingTarget + sine * target;
    target = -sine * existingTarget + cosine * target;
  }
}

auto SolveQr(const TriangularMatrix& upper, const BasisVector& transformedTarget, BasisVector& solution) noexcept
    -> bool {
  double largestDiagonal = 0.0;
  for (size_t index = 0; index < PrismNdfCoefficientCount; ++index) {
    largestDiagonal = (std::max)(largestDiagonal, std::abs(upper[index][index]));
  }
  if (!std::isfinite(largestDiagonal) || largestDiagonal == 0.0) {
    return false;
  }
  const double rankTolerance = largestDiagonal * std::numeric_limits<double>::epsilon() * PrismNdfCoefficientCount;
  for (size_t reverse = PrismNdfCoefficientCount; reverse-- > 0;) {
    double value = transformedTarget[reverse];
    for (size_t column = reverse + 1; column < PrismNdfCoefficientCount; ++column) {
      value -= upper[reverse][column] * solution[column];
    }
    const double diagonal = upper[reverse][reverse];
    if (!std::isfinite(diagonal) || std::abs(diagonal) <= rankTolerance) {
      return false;
    }
    solution[reverse] = value / diagonal;
    if (!std::isfinite(solution[reverse])) {
      return false;
    }
  }
  return true;
}

auto SampleGrid(std::span<const float> grid, double x, double y) -> double {
  if (x * x + y * y > 1.0) {
    return 0.0;
  }
  const double px = (x + 1.0) * 0.5 * static_cast<double>(PrismNdfGridSize - 1);
  const double py = (y + 1.0) * 0.5 * static_cast<double>(PrismNdfGridSize - 1);
  const uint32_t x0 = static_cast<uint32_t>(std::floor(px));
  const uint32_t y0 = static_cast<uint32_t>(std::floor(py));
  const uint32_t x1 = (std::min)(x0 + 1, PrismNdfGridSize - 1);
  const uint32_t y1 = (std::min)(y0 + 1, PrismNdfGridSize - 1);
  const double tx = px - x0;
  const double ty = py - y0;
  const auto at = [&grid](uint32_t column, uint32_t row) {
    return static_cast<double>(grid[static_cast<size_t>(row) * PrismNdfGridSize + column]);
  };
  return std::lerp(std::lerp(at(x0, y0), at(x1, y0), tx), std::lerp(at(x0, y1), at(x1, y1), tx), ty);
}
} // namespace

auto EvaluatePrismZernikeBasis(uint32_t coefficientIndex, double x, double y) noexcept -> double {
  if (coefficientIndex >= PrismNdfCoefficientCount || !std::isfinite(x) || !std::isfinite(y)) {
    return 0.0;
  }
  uint32_t index = 0;
  for (uint32_t n = 0; n <= PrismNdfDegree; ++n) {
    for (int32_t m = -static_cast<int32_t>(n); m <= static_cast<int32_t>(n); m += 2, ++index) {
      if (index == coefficientIndex) {
        return EvaluateBasis(n, m, x, y);
      }
    }
  }
  return 0.0;
}

auto BuildPrismNdfResidualMipChain(std::span<const float> residuals) -> std::vector<std::vector<float>> {
  if (residuals.size() != PrismNdfSampleCount) {
    return {};
  }
  std::vector<std::vector<float>> levels;
  levels.emplace_back(residuals.begin(), residuals.end());
  uint32_t size = PrismNdfGridSize;
  while (size > 1) {
    const uint32_t nextSize = size / 2;
    auto& previous = levels.back();
    std::vector<float> next(static_cast<size_t>(nextSize) * nextSize);
    for (uint32_t y = 0; y < nextSize; ++y) {
      for (uint32_t x = 0; x < nextSize; ++x) {
        const size_t top = (static_cast<size_t>(y) * 2U) * size + static_cast<size_t>(x) * 2U;
        next[static_cast<size_t>(y) * nextSize + x] =
            0.25F * (previous[top] + previous[top + 1] + previous[top + size] + previous[top + size + 1]);
      }
    }
    levels.push_back(std::move(next));
    size = nextSize;
  }
  return levels;
}

namespace {
auto MapPreviewResidualUv(const PrismNdfData& data, float x, float y) -> std::array<float, 2> {
  const float radius = std::sqrt(x * x + y * y);
  if (data.equalAngleResiduals && radius > 1.0e-6F) {
    const float mappedRadius = std::asin(std::min(radius, 1.0F)) * (2.0F / std::numbers::pi_v<float>);
    x *= mappedRadius / radius;
    y *= mappedRadius / radius;
  }
  float u = x * 0.5F + 0.5F;
  float v = y * 0.5F + 0.5F;
  if (data.residualFilter == PrismNdfResidualFilter::QuinticUv) {
    const auto warp = [](float coordinate) {
      const float texel = coordinate * static_cast<float>(PrismNdfGridSize) - 0.5F;
      const float fraction = texel - std::floor(texel);
      const float square = fraction * fraction;
      const float warped = square * fraction * (fraction * (fraction * 6.0F - 15.0F) + 10.0F);
      return (std::floor(texel) + warped + 0.5F) / static_cast<float>(PrismNdfGridSize);
    };
    u = warp(u);
    v = warp(v);
  }
  return {u, v};
}

auto SamplePreviewResidual(const std::vector<std::vector<float>>& mipLevels, const std::array<float, 2>& uv,
                           uint32_t mip) -> double {
  const auto& level = mipLevels[mip];
  const uint32_t size = PrismNdfGridSize >> mip;
  const float x = uv[0] * static_cast<float>(size) - 0.5F;
  const float y = uv[1] * static_cast<float>(size) - 0.5F;
  const float floorX = std::floor(x);
  const float floorY = std::floor(y);
  const int32_t x0 = static_cast<int32_t>(floorX);
  const int32_t y0 = static_cast<int32_t>(floorY);
  const auto clampIndex = [size](int32_t index) {
    return static_cast<uint32_t>(std::clamp(index, 0, static_cast<int32_t>(size) - 1));
  };
  const uint32_t x1 = clampIndex(x0 + 1);
  const uint32_t y1 = clampIndex(y0 + 1);
  const uint32_t sx0 = clampIndex(x0);
  const uint32_t sy0 = clampIndex(y0);
  const double tx = x - floorX;
  const double ty = y - floorY;
  const auto at = [&level, size](uint32_t sx, uint32_t sy) {
    return static_cast<double>(level[static_cast<size_t>(sy) * size + sx]);
  };
  const double top = std::lerp(at(sx0, sy0), at(x1, sy0), tx);
  const double bottom = std::lerp(at(sx0, y1), at(x1, y1), tx);
  return std::lerp(top, bottom, ty);
}
} // namespace

auto EvaluatePrismNdfPreview(const PrismNdfData& data, const std::vector<std::vector<float>>& mipLevels, float x,
                             float y, float footprint) -> float {
  if (data.residuals.size() != PrismNdfSampleCount || x * x + y * y > 1.0F || mipLevels.empty() ||
      !std::isfinite(footprint) || footprint <= 0.0F) {
    return 0.0F;
  }
  double value = 0.0;
  for (uint32_t index = 0; index < PrismNdfCoefficientCount; ++index) {
    value += static_cast<double>(data.zernikeCoefficients[index]) * EvaluatePrismZernikeBasis(index, x, y);
  }
  const float radius = std::sqrt(x * x + y * y);
  const auto uv = MapPreviewResidualUv(data, x, y);
  const auto uvX = MapPreviewResidualUv(data, x + footprint, y);
  const auto uvY = MapPreviewResidualUv(data, x, y + footprint);
  const float gradientX = std::hypot((uvX[0] - uv[0]) * static_cast<float>(PrismNdfGridSize),
                                     (uvX[1] - uv[1]) * static_cast<float>(PrismNdfGridSize));
  const float gradientY = std::hypot((uvY[0] - uv[0]) * static_cast<float>(PrismNdfGridSize),
                                     (uvY[1] - uv[1]) * static_cast<float>(PrismNdfGridSize));
  const float rho = (std::max)(gradientX, gradientY);
  const float maxMip = static_cast<float>(mipLevels.size() - 1);
  const float lod = data.mipmappedResiduals ? std::clamp(std::log2((std::max)(rho, 1.0F)), 0.0F, maxMip) : 0.0F;
  const uint32_t lowerMip = static_cast<uint32_t>(lod);
  const uint32_t upperMip = (std::min)(lowerMip + 1, static_cast<uint32_t>(mipLevels.size() - 1));
  const double blend = static_cast<double>(lod - static_cast<float>(lowerMip));
  const double lowerResidual = SamplePreviewResidual(mipLevels, uv, lowerMip);
  const double upperResidual = SamplePreviewResidual(mipLevels, uv, upperMip);
  const float fadeT = std::clamp(std::sqrt((std::max)(0.0F, 1.0F - radius * radius)) / 0.1F, 0.0F, 1.0F);
  const float horizon = data.horizonFade ? fadeT * fadeT * (3.0F - 2.0F * fadeT) : 1.0F;
  const double residual = data.residualsEnabled ? std::lerp(lowerResidual, upperResidual, blend) * horizon : 0.0;
  value = (std::max)(0.0, value + residual);
  return static_cast<float>(value * data.normalization);
}
auto FitPrismNdfPayload(std::span<const float> grid, PrismNdfData& output, std::string& error) -> bool {
  error.clear();
  if (!IsFiniteGrid(grid, error)) {
    return false;
  }
  if (std::ranges::none_of(grid, [](float value) { return value > 0.0F; })) {
    error = "PRISM NDF grid must contain positive energy";
    return false;
  }
  TriangularMatrix upper{};
  BasisVector transformedTarget{};
  for (uint32_t row = 0; row < PrismNdfGridSize; ++row) {
    const double y = 2.0 * static_cast<double>(row) / static_cast<double>(PrismNdfGridSize - 1) - 1.0;
    for (uint32_t column = 0; column < PrismNdfGridSize; ++column) {
      const double x = 2.0 * static_cast<double>(column) / static_cast<double>(PrismNdfGridSize - 1) - 1.0;
      if (x * x + y * y > 1.0) {
        continue;
      }
      const BasisVector basis = BasisAt(x, y);
      const double value = grid[static_cast<size_t>(row) * PrismNdfGridSize + column];
      AccumulateQrRow(upper, transformedTarget, basis, value);
    }
  }
  BasisVector coefficients{};
  if (!SolveQr(upper, transformedTarget, coefficients)) {
    error = "PRISM NDF least-squares projection is rank deficient or non-finite";
    return false;
  }
  PrismNdfData result;
  for (size_t index = 0; index < coefficients.size(); ++index) {
    if (!std::isfinite(coefficients[index]) || std::abs(coefficients[index]) > std::numeric_limits<float>::max()) {
      error = "PRISM NDF coefficient fit produced an unrepresentable value";
      return false;
    }
    result.zernikeCoefficients[index] = static_cast<float>(coefficients[index]);
  }
  result.residuals.resize(PrismNdfSampleCount);
  for (uint32_t row = 0; row < PrismNdfGridSize; ++row) {
    const double y = 2.0 * static_cast<double>(row) / static_cast<double>(PrismNdfGridSize - 1) - 1.0;
    for (uint32_t column = 0; column < PrismNdfGridSize; ++column) {
      const size_t sampleIndex = static_cast<size_t>(row) * PrismNdfGridSize + column;
      const double x = 2.0 * static_cast<double>(column) / static_cast<double>(PrismNdfGridSize - 1) - 1.0;
      if (x * x + y * y > 1.0) {
        continue;
      }
      const BasisVector basis = BasisAt(x, y);
      double polynomial = 0.0;
      for (size_t index = 0; index < coefficients.size(); ++index) {
        polynomial += static_cast<double>(result.zernikeCoefficients[index]) * basis[index];
      }
      result.residuals[sampleIndex] = grid[sampleIndex] - static_cast<float>(polynomial);
    }
  }
  output = std::move(result);
  return true;
}

auto ComputePrismProjectedEnergyNormalization(std::span<const float> grid, float& output, std::string& error) -> bool {
  error.clear();
  if (!IsFiniteGrid(grid, error)) {
    return false;
  }
  double integral = 0.0;
  constexpr double radialStep = 1.0 / static_cast<double>(RadialQuadratureResolution);
  constexpr double angularStep = 2.0 * std::numbers::pi / static_cast<double>(AngularQuadratureResolution);
  for (uint32_t radialIndex = 0; radialIndex < RadialQuadratureResolution; ++radialIndex) {
    const double radius = (static_cast<double>(radialIndex) + 0.5) * radialStep;
    for (uint32_t angularIndex = 0; angularIndex < AngularQuadratureResolution; ++angularIndex) {
      const double angle = (static_cast<double>(angularIndex) + 0.5) * angularStep;
      integral +=
          SampleGrid(grid, radius * std::cos(angle), radius * std::sin(angle)) * radius * radialStep * angularStep;
    }
  }
  if (!std::isfinite(integral) || integral <= 1.0e-12) {
    error = "PRISM NDF projected-disk energy must be finite and greater than zero";
    return false;
  }
  const double normalization = 1.0 / integral;
  if (!std::isfinite(normalization) || normalization > std::numeric_limits<float>::max()) {
    error = "PRISM NDF projected-energy normalization is not representable";
    return false;
  }
  output = static_cast<float>(normalization);
  return true;
}

} // namespace Engine::Renderer

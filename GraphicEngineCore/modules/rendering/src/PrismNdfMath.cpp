#include "rendering/include/renderer/PrismNdfMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

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
    error = "PRISM NDF grid must contain exactly 4096 samples";
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
  output = result;
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

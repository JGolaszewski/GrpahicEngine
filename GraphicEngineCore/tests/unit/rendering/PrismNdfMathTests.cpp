#include "rendering/include/renderer/PrismNdfMath.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace {
auto Factorial(uint32_t value) -> double {
  double result = 1.0;
  for (uint32_t factor = 2; factor <= value; ++factor) {
    result *= factor;
  }
  return result;
}

auto IndependentBasis(uint32_t n, int32_t m, double x, double y) -> double {
  const uint32_t absoluteM = static_cast<uint32_t>(std::abs(m));
  const double radiusSquared = x * x + y * y;
  if (radiusSquared > 1.0) {
    return 0.0;
  }
  const double radius = std::sqrt(radiusSquared);
  double radial = 0.0;
  for (uint32_t k = 0; k <= (n - absoluteM) / 2; ++k) {
    const double sign = (k % 2 == 0) ? 1.0 : -1.0;
    const double numerator = Factorial(n - k);
    const double denominator = Factorial(k) * Factorial((n + absoluteM) / 2 - k) * Factorial((n - absoluteM) / 2 - k);
    radial += sign * numerator / denominator * std::pow(radius, static_cast<int>(n - 2 * k));
  }
  const double angle = std::atan2(y, x);
  return radial * (m < 0 ? std::sin(absoluteM * angle) : std::cos(absoluteM * angle));
}

auto GridAt(std::span<const float> grid, double x, double y) -> double {
  if (x * x + y * y > 1.0) {
    return 0.0;
  }
  const double px = (x + 1.0) * 0.5 * 63.0;
  const double py = (y + 1.0) * 0.5 * 63.0;
  const uint32_t x0 = static_cast<uint32_t>(std::floor(px));
  const uint32_t y0 = static_cast<uint32_t>(std::floor(py));
  const uint32_t x1 = (std::min)(x0 + 1, 63U);
  const uint32_t y1 = (std::min)(y0 + 1, 63U);
  const double tx = px - x0;
  const double ty = py - y0;
  const auto at = [&grid](uint32_t column, uint32_t row) { return static_cast<double>(grid[row * 64 + column]); };
  return std::lerp(std::lerp(at(x0, y0), at(x1, y0), tx), std::lerp(at(x0, y1), at(x1, y1), tx), ty);
}
} // namespace

TEST(PrismNdfMathTests, AllFortyFiveBasisTermsMatchIndependentFactorialDefinition) {
  const std::array points{std::array{0.0, 0.0}, std::array{0.31, -0.47}, std::array{0.6, 0.8},
                          std::array{-1.0, 0.0}, std::array{0.0, 1.0}, std::array{0.6, 0.8 + 1.0e-12},
                          std::array{1.01, -0.2}};
  uint32_t coefficientIndex = 0;
  for (uint32_t n = 0; n <= Engine::PrismNdfDegree; ++n) {
    for (int32_t m = -static_cast<int32_t>(n); m <= static_cast<int32_t>(n); m += 2, ++coefficientIndex) {
      for (const auto& point : points) {
        EXPECT_NEAR(Engine::Renderer::EvaluatePrismZernikeBasis(coefficientIndex, point[0], point[1]),
                    IndependentBasis(n, m, point[0], point[1]), 1.0e-11)
            << "n=" << n << " m=" << m << " x=" << point[0] << " y=" << point[1];
      }
    }
  }
  EXPECT_EQ(coefficientIndex, Engine::PrismNdfCoefficientCount);
}

TEST(PrismNdfMathTests, ResidualMaskClampAndNormalizationMatchIndependentDoubleReference) {
  const std::array points{std::array{0.0, 0.0}, std::array{0.31, -0.47}, std::array{0.6, 0.8},
                          std::array{-1.0, 0.0}, std::array{0.0, 1.0}, std::array{1.0001, 0.0}};
  std::array<float, Engine::PrismNdfCoefficientCount> coefficients{};
  for (size_t index = 0; index < coefficients.size(); ++index) {
    coefficients[index] = static_cast<float>((static_cast<int>(index % 9) - 4) * 0.0375);
  }
  constexpr double residual = -0.025;
  constexpr double normalization = 1.375;
  for (const auto& point : points) {
    double implementationPolynomial = 0.0;
    double referencePolynomial = 0.0;
    uint32_t coefficient = 0;
    for (uint32_t n = 0; n <= Engine::PrismNdfDegree; ++n) {
      for (int32_t m = -static_cast<int32_t>(n); m <= static_cast<int32_t>(n); m += 2, ++coefficient) {
        implementationPolynomial += static_cast<double>(coefficients[coefficient]) *
                                    Engine::Renderer::EvaluatePrismZernikeBasis(coefficient, point[0], point[1]);
        referencePolynomial += static_cast<double>(coefficients[coefficient]) *
                               IndependentBasis(n, m, point[0], point[1]);
      }
    }
    const double diskMask = point[0] * point[0] + point[1] * point[1] <= 1.0 ? 1.0 : 0.0;
    const double implementation = std::max(implementationPolynomial + residual, 0.0) * diskMask * normalization;
    const double reference = std::max(referencePolynomial + residual, 0.0) * diskMask * normalization;
    EXPECT_NEAR(implementation, reference, 1.0e-11);
    EXPECT_GE(reference, 0.0);
    if (diskMask == 0.0) {
      EXPECT_DOUBLE_EQ(reference, 0.0);
    }
    EXPECT_NEAR(implementation / normalization, reference / normalization, 1.0e-11);
  }

  // Isolate signed residual, ReLU, mask, and scale semantics from the polynomial.
  const auto compose = [](double base, double signedResidual, double mask, double scale) {
    return std::max(base + signedResidual, 0.0) * mask * scale;
  };
  EXPECT_DOUBLE_EQ(compose(0.0, -0.25, 1.0, normalization), 0.0);
  EXPECT_DOUBLE_EQ(compose(0.0, 0.25, 1.0, normalization), 0.25 * normalization);
  EXPECT_DOUBLE_EQ(compose(0.0, 0.25, 0.0, normalization), 0.0);
}

TEST(PrismNdfMathTests, LeastSquaresCoefficientsAndSignedResidualReconstructEverySample) {
  std::array<float, Engine::PrismNdfSampleCount> grid{};
  for (uint32_t row = 0; row < Engine::PrismNdfGridSize; ++row) {
    const double y = 2.0 * row / 63.0 - 1.0;
    for (uint32_t column = 0; column < Engine::PrismNdfGridSize; ++column) {
      const double x = 2.0 * column / 63.0 - 1.0;
      if (x * x + y * y <= 1.0) {
        grid[row * Engine::PrismNdfGridSize + column] =
            static_cast<float>(std::exp(-3.0 * (x * x + 1.4 * y * y)) * (1.0 + 0.1 * x * y));
      }
    }
  }
  Engine::PrismNdfData payload;
  std::string error;
  ASSERT_TRUE(Engine::Renderer::FitPrismNdfPayload(grid, payload, error)) << error;
  bool hasNegativeResidual = false;
  bool hasPositiveResidual = false;
  for (uint32_t row = 0; row < Engine::PrismNdfGridSize; ++row) {
    const double y = 2.0 * row / 63.0 - 1.0;
    for (uint32_t column = 0; column < Engine::PrismNdfGridSize; ++column) {
      const size_t sample = row * Engine::PrismNdfGridSize + column;
      const double x = 2.0 * column / 63.0 - 1.0;
      double base = 0.0;
      for (uint32_t coefficient = 0; coefficient < Engine::PrismNdfCoefficientCount; ++coefficient) {
        base +=
            payload.zernikeCoefficients[coefficient] * Engine::Renderer::EvaluatePrismZernikeBasis(coefficient, x, y);
      }
      EXPECT_NEAR(base + payload.residuals[sample], grid[sample], 2.0e-6);
      hasNegativeResidual |= payload.residuals[sample] < -1.0e-7F;
      hasPositiveResidual |= payload.residuals[sample] > 1.0e-7F;
    }
  }
  EXPECT_TRUE(hasNegativeResidual);
  EXPECT_TRUE(hasPositiveResidual);
}

TEST(PrismNdfMathTests, StreamingQrRecoversIndependentKnownCoefficientOracle) {
  std::array<double, Engine::PrismNdfCoefficientCount> oracle{};
  oracle[0] = 1.75;
  oracle[1] = 0.08;
  oracle[3] = -0.035;
  oracle[9] = 0.012;
  std::array<float, Engine::PrismNdfSampleCount> grid{};
  for (uint32_t row = 0; row < Engine::PrismNdfGridSize; ++row) {
    const double y = 2.0 * row / 63.0 - 1.0;
    for (uint32_t column = 0; column < Engine::PrismNdfGridSize; ++column) {
      const double x = 2.0 * column / 63.0 - 1.0;
      if (x * x + y * y > 1.0) {
        continue;
      }
      double expected = 0.0;
      uint32_t index = 0;
      for (uint32_t n = 0; n <= Engine::PrismNdfDegree; ++n) {
        for (int32_t m = -static_cast<int32_t>(n); m <= static_cast<int32_t>(n); m += 2, ++index) {
          expected += oracle[index] * IndependentBasis(n, m, x, y);
        }
      }
      grid[row * Engine::PrismNdfGridSize + column] = static_cast<float>(expected);
    }
  }
  Engine::PrismNdfData payload;
  std::string error;
  ASSERT_TRUE(Engine::Renderer::FitPrismNdfPayload(grid, payload, error)) << error;
  for (size_t index = 0; index < oracle.size(); ++index) {
    EXPECT_NEAR(payload.zernikeCoefficients[index], oracle[index], 2.0e-6) << "coefficient=" << index;
  }
}

TEST(PrismNdfMathTests, ProjectedEnergyNormalizationIsFiniteAndUnitIntegral) {
  std::array<float, Engine::PrismNdfSampleCount> grid{};
  for (uint32_t row = 0; row < Engine::PrismNdfGridSize; ++row) {
    const double y = 2.0 * row / 63.0 - 1.0;
    for (uint32_t column = 0; column < Engine::PrismNdfGridSize; ++column) {
      const double x = 2.0 * column / 63.0 - 1.0;
      if (x * x + y * y <= 1.0) {
        grid[row * Engine::PrismNdfGridSize + column] = static_cast<float>(std::exp(-5.0 * (x * x + y * y)));
      }
    }
  }
  float normalization = 0.0F;
  std::string error;
  ASSERT_TRUE(Engine::Renderer::ComputePrismProjectedEnergyNormalization(grid, normalization, error)) << error;
  ASSERT_TRUE(std::isfinite(normalization));
  constexpr uint32_t radialSamples = 1536;
  constexpr uint32_t angularSamples = 3072;
  constexpr double radialStep = 1.0 / radialSamples;
  constexpr double angularStep = 2.0 * std::numbers::pi / angularSamples;
  double integral = 0.0;
  for (uint32_t radial = 0; radial < radialSamples; ++radial) {
    const double radius = (radial + 0.5) * radialStep;
    for (uint32_t angular = 0; angular < angularSamples; ++angular) {
      const double angle = (angular + 0.5) * angularStep;
      integral += GridAt(grid, radius * std::cos(angle), radius * std::sin(angle)) * normalization * radius *
                  radialStep * angularStep;
    }
  }
  EXPECT_NEAR(integral, 1.0, 1.0e-4);
}

TEST(PrismNdfMathTests, InvalidGridValuesAreRejected) {
  std::vector<float> grid(Engine::PrismNdfSampleCount, 1.0F);
  Engine::PrismNdfData payload;
  payload.normalization = 3.0F;
  const Engine::PrismNdfData original = payload;
  std::string error;
  grid[0] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(Engine::Renderer::FitPrismNdfPayload(grid, payload, error));
  EXPECT_FALSE(error.empty());
  EXPECT_EQ(payload, original);
  grid[0] = -1.0F;
  EXPECT_FALSE(Engine::Renderer::FitPrismNdfPayload(grid, payload, error));
  EXPECT_EQ(payload, original);
  std::ranges::fill(grid, 0.0F);
  EXPECT_FALSE(Engine::Renderer::FitPrismNdfPayload(grid, payload, error));
  EXPECT_EQ(payload, original);
}

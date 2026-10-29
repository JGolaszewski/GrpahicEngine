#pragma once

#include "runtime/include/CoreExport.h"
#include "runtime/include/PrismNdfData.h"

#include <span>
#include <string>
#include <vector>

namespace Engine::Renderer {

/// Fits the canonical degree-8 basis with streaming Givens QR and stores a signed residual
/// that reconstructs each in-disk source sample within float storage precision. Returns false
/// for malformed or zero-energy grids, rank-deficient fits, or values that cannot be represented in the payload.
ENGINE_API auto FitPrismNdfPayload(std::span<const float> grid, PrismNdfData& output, std::string& error) -> bool;

/// Computes the projected-disk energy scale using deterministic bilinear midpoint quadrature.
ENGINE_API auto ComputePrismProjectedEnergyNormalization(std::span<const float> grid, float& output, std::string& error)
    -> bool;

/// Evaluates one canonical real Zernike term using recurrence-only polynomial math.
ENGINE_API auto EvaluatePrismZernikeBasis(uint32_t coefficientIndex, double x, double y) noexcept -> double;

/// Builds the complete power-of-two mip chain for a signed residual grid by averaging each 2x2 footprint.
/// Returns an empty chain unless the input contains exactly PrismNdfSampleCount samples.
ENGINE_API auto BuildPrismNdfResidualMipChain(std::span<const float> residuals) -> std::vector<std::vector<float>>;

} // namespace Engine::Renderer

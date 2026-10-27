#pragma once

#include "runtime/include/CoreExport.h"
#include "runtime/include/PrismNdfData.h"

#include <span>
#include <string>

namespace Engine::Renderer {

/// Fits the canonical PRISM degree-8 basis and stores an exact signed grid residual.
ENGINE_API auto FitPrismNdfPayload(std::span<const float> grid, PrismNdfData& output, std::string& error) -> bool;

/// Computes the projected-disk energy scale using deterministic bilinear midpoint quadrature.
ENGINE_API auto ComputePrismProjectedEnergyNormalization(std::span<const float> grid, float& output, std::string& error)
    -> bool;

/// Evaluates one canonical real Zernike term using recurrence-only polynomial math.
ENGINE_API auto EvaluatePrismZernikeBasis(uint32_t coefficientIndex, double x, double y) noexcept -> double;

} // namespace Engine::Renderer

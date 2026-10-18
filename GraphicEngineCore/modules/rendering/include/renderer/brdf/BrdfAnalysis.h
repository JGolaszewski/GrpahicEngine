#pragma once

#include "rendering/include/RenderOutput.h"
#include "rendering/include/renderer/Material.h"
#include "runtime/include/Result.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Engine::Renderer::BrdfAnalysis {

/// Summary statistics for a sampled BRDF response curve.
struct BrdfCurveMeasurements {
  float Peak = 0.0F;     ///< Largest sampled response value.
  float Mean = 0.0F;     ///< Arithmetic mean of the sampled response values.
  float Integral = 0.0F; ///< Trapezoidal response integral over view/light angle in radians.
};

/// BRDF luminance response measured while sweeping the incident light angle.
struct BrdfResponseCurve {
  BrdfModel Model = BrdfModel::Legacy; ///< BRDF model used for all samples.
  std::vector<float> AnglesDegrees;    ///< Uniformly spaced incident angles from 0 to 90 degrees.
  std::vector<float> Response;         ///< Rec. 709 luminance of the unweighted BRDF at each angle.
  BrdfCurveMeasurements Measurements;  ///< Summary of the response samples.
};

/// Mean and peak linear luminance measured from a rendered scene image.
struct SceneLuminanceMeasurements {
  float Mean = 0.0F; ///< Arithmetic mean of per-pixel Rec. 709 luminance.
  float Peak = 0.0F; ///< Maximum per-pixel Rec. 709 luminance.
};

/// Samples the selected BRDF with a fixed normal-facing view and a swept light angle.
///
/// GGX and GTR use the engine's microfacet response. Legacy uses the legacy Lambert
/// and powered specular response. The reported curve is the unweighted BRDF value;
/// the incident-angle cosine is not applied. Input material data is only read and
/// sanitized locally, so authored properties remain unchanged.
/// @param material Material properties supplying color, roughness, and reflectance.
/// @param model BRDF model evaluated for this curve.
/// @param sampleCount Number of uniformly spaced samples, including 0 and 90 degrees; must be in [2, 4096].
/// @return Response curve and summary measurements, or an error for an unsupported model or invalid sample count.
ENGINE_API Result<BrdfResponseCurve> AnalyzeBrdfResponse(const MaterialProperties& material, BrdfModel model,
                                                         uint32_t sampleCount = 91);

/// Measures linear Rec. 709 luminance from tightly packed, top-to-bottom RGBA pixels.
///
/// Non-finite samples, zero dimensions, or a pixel span whose length does not match
/// width * height * 4 produce an error. Negative RGB components contribute as zero.
/// @param width Image width in pixels.
/// @param height Image height in pixels.
/// @param rgba Linear RGBA float samples.
/// @return Mean and peak per-pixel luminance, or an error for malformed input.
ENGINE_API Result<SceneLuminanceMeasurements> MeasureSceneLuminance(uint32_t width, uint32_t height,
                                                                    std::span<const float> rgba);

/// Measures the linear final-color plane of a completed render output.
/// @param output CPU-owned render result.
/// @return Mean and peak per-pixel luminance, or an error when final-color data is unavailable or malformed.
ENGINE_API Result<SceneLuminanceMeasurements> MeasureSceneLuminance(const Engine::RenderOutput& output);

} // namespace Engine::Renderer::BrdfAnalysis

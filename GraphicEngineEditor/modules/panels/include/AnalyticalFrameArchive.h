#pragma once

#include "rendering/include/renderer/brdf/BrdfAnalysis.h"
#include "rendering/include/renderer/brdf/BrdfAnalysisSnapshot.h"
#include "runtime/include/Result.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Editor::Panels {

/// One completed model's captured planes and analytic measurements.
struct AnalyticalFrameVariant {
  Engine::Renderer::BrdfAnalysis::BrdfAnalysisVariant Render; ///< Exact per-model render request and CPU-owned output.
  Engine::Renderer::BrdfAnalysis::BrdfResponseCurve Curve;    ///< Analytic BRDF response and curve measurements.
  Engine::Renderer::BrdfAnalysis::SceneLuminanceMeasurements SceneLuminance; ///< Captured final-color luminance.
};

/// Complete detached project state for static Analytical Frame inspection.
/// The viewport object uses the field names and enum strings documented by
/// AnalyticalFrameViewState. Its values are validated by AnalyticalFrameArchive.
struct AnalyticalFrameSession {
  Engine::Renderer::BrdfAnalysis::BrdfAnalysisSnapshot Snapshot; ///< Authored scene/material and shared render inputs.
  std::string TargetLabel;                      ///< User-facing target description captured with the frame.
  std::vector<AnalyticalFrameVariant> Variants; ///< Complete ordered model outputs and measurements.
  nlohmann::json ViewportState =
      nlohmann::json::object();            ///< Persisted view selection, navigation, and comparison state.
  bool PostProcessCallbackPresent = false; ///< Records an unavailable callable; reopened frames never invoke it.
};

/// Saves and reopens self-contained Analytical Frame projects without a renderer.
class AnalyticalFrameArchive final {
public:
  /// Saves a fully validated session as a versioned single-file .aframe project.
  /// @param path Destination .aframe path.
  /// @param session Completed frame to serialize.
  /// @return Success or a validation, serialization, or file error.
  static auto Save(const std::filesystem::path& path, const AnalyticalFrameSession& session) -> Engine::Status;

  /// Loads and validates a complete detached frame project.
  /// @param path Source .aframe path.
  /// @return Fully owned static-inspection session or a diagnostic error; no partial session is returned.
  static auto Load(const std::filesystem::path& path) -> Engine::Result<AnalyticalFrameSession>;
};

} // namespace Editor::Panels

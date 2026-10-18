#pragma once

#include "panels/include/BrdfAnalysisPanel.h"
#include "runtime/include/Result.h"

#include <filesystem>

/// Writes the displayed BRDF comparison as a PNG chart or a JSON report.
class BrdfAnalysisExporter final {
public:
  /// Exports the angle response curves using the panel's model colors and shared vertical scale.
  /// @param comparison Completed comparison currently shown in the panel.
  /// @param path Destination .png file.
  /// @return Success or a chart, path, or encoder error.
  static auto WriteChartPng(const BrdfAnalysisPanel::CompletedComparison& comparison, const std::filesystem::path& path)
      -> Engine::Status;

  /// Exports the captured scene, source settings, and displayed measurements as JSON.
  /// @param comparison Completed comparison currently shown in the panel.
  /// @param path Destination .json file.
  /// @return Success or a serialization, path, or file error.
  static auto WriteJson(const BrdfAnalysisPanel::CompletedComparison& comparison, const std::filesystem::path& path)
      -> Engine::Status;
};

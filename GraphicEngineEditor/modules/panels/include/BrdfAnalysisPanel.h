#pragma once

#include "panels/include/EditorPanel.h"
#include "rendering/include/renderer/brdf/BrdfAnalysis.h"
#include "rendering/include/renderer/brdf/BrdfAnalysisSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

/// Editor view for comparing BRDF response curves and scene render measurements.
class BrdfAnalysisPanel final : public EditorPanel {
public:
  struct Target {
    uint64_t EntityId = 0;                       ///< Document-local entity identifier containing the material.
    std::optional<size_t> MaterialOverrideIndex; ///< Submesh override slot, or empty for the entity material.
    std::string Label;                           ///< User-facing entity and material description.
  };

  /// One model's complete response and rendered-scene measurements.
  struct VariantResult {
    Engine::Renderer::BrdfAnalysis::BrdfAnalysisVariant Render; ///< Captured model request and CPU render output.
    Engine::Renderer::BrdfAnalysis::BrdfResponseCurve Curve;    ///< Angle-dependent BRDF response and curve statistics.
    Engine::Renderer::BrdfAnalysis::SceneLuminanceMeasurements SceneLuminance; ///< Render mean and peak luminance.
  };

  /// The exact successful comparison shown by this panel and available to exporters.
  struct CompletedComparison {
    Engine::Renderer::BrdfAnalysis::BrdfAnalysisSnapshot Snapshot; ///< Shared source scene and render settings.
    std::string TargetLabel;                                       ///< Target label captured when this comparison ran.
    std::vector<VariantResult> Variants;                           ///< All variants shown in the panel.
  };

  /// All-or-error output from the editor-owned capture and render callback.
  struct ExecutionResult {
    Engine::Renderer::BrdfAnalysis::BrdfAnalysisSnapshot Snapshot;             ///< Captured common inputs.
    std::vector<Engine::Renderer::BrdfAnalysis::BrdfAnalysisVariant> Variants; ///< Fully rendered variants.
  };

  /// Supplies available scene material targets.
  using TargetProvider = std::function<Engine::Result<std::vector<Target>>()>;
  /// Captures one target and renders all requested models from the same snapshot.
  using ExecuteFunction =
      std::function<Engine::Result<ExecutionResult>(const Target&, const std::vector<Engine::Renderer::BrdfModel>&)>;
  /// Exports the displayed result. A successful empty value means the user cancelled the file dialog.
  using ExportFunction = std::function<Engine::Result<std::optional<std::string>>(const CompletedComparison&, bool)>;

  /// Creates the panel with editor-owned target discovery and variant execution callbacks.
  BrdfAnalysisPanel(TargetProvider getTargets, ExecuteFunction execute);

  /// Replaces the editor-owned scene selection and render execution services.
  void SetServices(TargetProvider getTargets, ExecuteFunction execute);
  /// Sets the callback used to export the currently displayed chart or JSON report.
  void SetExportFunction(ExportFunction exportComparison);

  /// Draws target/model controls and the last fully completed comparison.
  void Draw() override;
  /// Invalidates cached target and result state when the active scene changes.
  void OnEvent(Engine::Events::Event& event) override;

  /// Gets the last fully measured comparison shown by the panel.
  /// @return The completed comparison, or empty when no successful run is displayed.
  [[nodiscard]] auto GetCompletedComparison() const noexcept -> const std::optional<CompletedComparison>& {
    return m_CompletedComparison;
  }

private:
  struct ModelSelection {
    Engine::Renderer::BrdfModel Model;
    bool Selected = true;
  };

  void RefreshTargets();
  void RunComparison();

  TargetProvider m_GetTargets;
  ExecuteFunction m_Execute;
  ExportFunction m_Export;
  std::vector<Target> m_Targets;
  std::optional<CompletedComparison> m_CompletedComparison;
  std::string m_Status = "Choose a material and run a comparison.";
  size_t m_SelectedTarget = 0;
  bool m_TargetsDirty = true;
  std::vector<ModelSelection> m_ModelSelections = {{Engine::Renderer::BrdfModel::Legacy},
                                                   {Engine::Renderer::BrdfModel::GGX},
                                                   {Engine::Renderer::BrdfModel::GTR},
                                                   {Engine::Renderer::BrdfModel::Beckmann},
                                                   {Engine::Renderer::BrdfModel::Gauss},
                                                   {Engine::Renderer::BrdfModel::BlinnPhong},
                                                   {Engine::Renderer::BrdfModel::AshikhminShirley},
                                                   {Engine::Renderer::BrdfModel::Schlick},
                                                   {Engine::Renderer::BrdfModel::Sawicki}};
};

#include "panels/include/BrdfAnalysisPanel.h"

#include "events/include/SceneChangedEvent.h"

#include <algorithm>
#include <array>
#include <imgui.h>
#include <span>
#include <utility>

namespace {
using Engine::Renderer::BrdfModel;

auto ModelName(BrdfModel model) -> const char* {
  switch (model) {
  case BrdfModel::Legacy:
    return "Legacy";
  case BrdfModel::GGX:
    return "GGX";
  case BrdfModel::GTR:
    return "GTR";
  case BrdfModel::Beckmann:
    return "Beckmann";
  case BrdfModel::Gauss:
    return "Gauss";
  case BrdfModel::BlinnPhong:
    return "Blinn-Phong";
  case BrdfModel::AshikhminShirley:
    return "Ashikhmin-Shirley";
  case BrdfModel::Schlick:
    return "Schlick";
  case BrdfModel::Sawicki:
    return "Sawicki";
  }
  return "Unknown";
}

auto ModelColor(BrdfModel model) -> ImU32 {
  switch (model) {
  case BrdfModel::Legacy:
    return IM_COL32(255, 196, 64, 255);
  case BrdfModel::GGX:
    return IM_COL32(77, 180, 255, 255);
  case BrdfModel::GTR:
    return IM_COL32(140, 225, 140, 255);
  case BrdfModel::Beckmann:
    return IM_COL32(229, 117, 128, 255);
  case BrdfModel::Gauss:
    return IM_COL32(192, 128, 255, 255);
  case BrdfModel::BlinnPhong:
    return IM_COL32(255, 155, 64, 255);
  case BrdfModel::AshikhminShirley:
    return IM_COL32(128, 220, 210, 255);
  case BrdfModel::Schlick:
    return IM_COL32(255, 128, 200, 255);
  case BrdfModel::Sawicki:
    return IM_COL32(180, 190, 100, 255);
  }
  return IM_COL32(220, 220, 220, 255);
}

void DrawResponseChart(std::span<const BrdfAnalysisPanel::VariantResult> variants) {
  constexpr float height = 220.0F;
  constexpr float leftPadding = 42.0F;
  constexpr float rightPadding = 12.0F;
  constexpr float bottomPadding = 28.0F;
  const float width = (std::max)(ImGui::GetContentRegionAvail().x, 180.0F);
  const ImVec2 size(width, height);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float legendMinimumX = origin.x + leftPadding;
  const float legendMaximumX = origin.x + width - rightPadding;
  size_t legendRows = 1;
  float legendCursorX = legendMinimumX;
  for (const auto& variant : variants) {
    const float labelWidth = ImGui::CalcTextSize(ModelName(variant.Curve.Model)).x + 22.0F;
    if (legendCursorX + labelWidth > legendMaximumX && legendCursorX > legendMinimumX) {
      ++legendRows;
      legendCursorX = legendMinimumX;
    }
    legendCursorX += labelWidth;
  }
  const float topPadding = static_cast<float>(legendRows) * 14.0F + 6.0F;
  ImGui::InvisibleButton("##BrdfResponseChart", size);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImVec2 plotMinimum(origin.x + leftPadding, origin.y + topPadding);
  const ImVec2 plotMaximum(origin.x + width - rightPadding, origin.y + height - bottomPadding);
  drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(22, 25, 30, 255));
  drawList->AddRect(plotMinimum, plotMaximum, IM_COL32(105, 112, 120, 255));

  float responseMaximum = 0.0F;
  for (const auto& variant : variants) {
    responseMaximum = (std::max)(responseMaximum, variant.Curve.Measurements.Peak);
  }
  responseMaximum = responseMaximum > 0.0F ? responseMaximum : 1.0F;
  for (int tick = 1; tick < 4; ++tick) {
    const float fraction = static_cast<float>(tick) / 4.0F;
    const float y = plotMaximum.y - fraction * (plotMaximum.y - plotMinimum.y);
    drawList->AddLine(ImVec2(plotMinimum.x, y), ImVec2(plotMaximum.x, y), IM_COL32(62, 67, 74, 255));
  }
  constexpr std::array<std::pair<const char*, float>, 3> angleTicks{{{"0°", 0.0F}, {"45°", 0.5F}, {"90°", 1.0F}}};
  for (const auto& [angle, fraction] : angleTicks) {
    const float x = plotMinimum.x + fraction * (plotMaximum.x - plotMinimum.x);
    drawList->AddText(ImVec2(x - 7.0F, plotMaximum.y + 6.0F), IM_COL32(190, 195, 202, 255), angle);
  }

  for (const auto& variant : variants) {
    const auto& curve = variant.Curve;
    if (curve.AnglesDegrees.size() != curve.Response.size() || curve.Response.size() < 2) {
      continue;
    }
    std::vector<ImVec2> points;
    points.reserve(curve.Response.size());
    for (size_t index = 0; index < curve.Response.size(); ++index) {
      const float xFraction = curve.AnglesDegrees.at(index) / 90.0F;
      const float yFraction = curve.Response.at(index) / responseMaximum;
      points.emplace_back(plotMinimum.x + xFraction * (plotMaximum.x - plotMinimum.x),
                          plotMaximum.y - yFraction * (plotMaximum.y - plotMinimum.y));
    }
    drawList->AddPolyline(points.data(), static_cast<int>(points.size()), ModelColor(curve.Model), ImDrawFlags_None,
                          2.0F);
  }
  float legendX = legendMinimumX;
  float legendY = origin.y + 1.0F;
  for (const auto& variant : variants) {
    const char* name = ModelName(variant.Curve.Model);
    const float labelWidth = ImGui::CalcTextSize(name).x + 22.0F;
    if (legendX + labelWidth > legendMaximumX && legendX > legendMinimumX) {
      legendX = legendMinimumX;
      legendY += 14.0F;
    }
    drawList->AddText(ImVec2(legendX, legendY), ModelColor(variant.Curve.Model), name);
    legendX += labelWidth;
  }
}
} // namespace

BrdfAnalysisPanel::BrdfAnalysisPanel(TargetProvider getTargets, ExecuteFunction execute)
    : m_GetTargets(std::move(getTargets)), m_Execute(std::move(execute)) {}

void BrdfAnalysisPanel::SetServices(TargetProvider getTargets, ExecuteFunction execute) {
  m_GetTargets = std::move(getTargets);
  m_Execute = std::move(execute);
  m_TargetsDirty = true;
  m_Targets.clear();
  m_SelectedTarget = 0;
  m_CompletedComparison.reset();
  m_Status = "Choose a material and run a comparison.";
}

void BrdfAnalysisPanel::SetExportFunction(ExportFunction exportComparison) { m_Export = std::move(exportComparison); }

void BrdfAnalysisPanel::OnEvent(Engine::Events::Event& event) {
  Engine::Events::EventDispatcher dispatcher(event);
  dispatcher.Dispatch<SceneChangedEvent>([this](const auto&) -> bool {
    m_TargetsDirty = true;
    m_Targets.clear();
    m_SelectedTarget = 0;
    m_CompletedComparison.reset();
    m_Status = "Scene changed. Choose a material and run a new comparison.";
    return false;
  });
}

void BrdfAnalysisPanel::RefreshTargets() {
  m_TargetsDirty = false;
  m_Targets.clear();
  m_SelectedTarget = 0;
  m_CompletedComparison.reset();
  if (!m_GetTargets) {
    m_Status = "Material selection is unavailable.";
    return;
  }
  auto targets = m_GetTargets();
  if (!targets) {
    m_Status = targets.error().Message;
    return;
  }
  m_Targets = std::move(*targets);
  m_Status =
      m_Targets.empty() ? "The active scene has no analyzable materials." : "Choose a material and run a comparison.";
}

void BrdfAnalysisPanel::RunComparison() {
  m_CompletedComparison.reset();
  if (m_SelectedTarget >= m_Targets.size()) {
    m_Status = "Choose a material before running the analysis.";
    return;
  }

  std::vector<BrdfModel> models;
  models.reserve(m_ModelSelections.size());
  for (const ModelSelection& selection : m_ModelSelections) {
    if (selection.Selected) {
      models.push_back(selection.Model);
    }
  }
  if (models.empty()) {
    m_Status = "Select at least one BRDF model.";
    return;
  }
  if (!m_Execute) {
    m_Status = "Scene capture and rendering are unavailable.";
    return;
  }

  m_Status = "Capturing the scene and rendering all selected variants...";
  auto execution = m_Execute(m_Targets.at(m_SelectedTarget), models);
  if (!execution) {
    m_Status = execution.error().Message;
    return;
  }
  if (execution->Variants.size() != models.size()) {
    m_Status = "Analysis failed: the renderer returned an incomplete variant set.";
    return;
  }

  CompletedComparison completed;
  completed.Snapshot = std::move(execution->Snapshot);
  completed.TargetLabel = m_Targets.at(m_SelectedTarget).Label;
  completed.Variants.reserve(execution->Variants.size());
  for (size_t index = 0; index < execution->Variants.size(); ++index) {
    auto& variant = execution->Variants.at(index);
    if (variant.Request.Model != models.at(index)) {
      m_Status = "Analysis failed: the renderer returned variants in an unexpected order.";
      return;
    }
    auto curve = Engine::Renderer::BrdfAnalysis::AnalyzeBrdfResponse(variant.Request.Material, variant.Request.Model);
    if (!curve) {
      m_Status = "BRDF curve calculation failed: " + curve.error().Message;
      return;
    }
    auto luminance = Engine::Renderer::BrdfAnalysis::MeasureSceneLuminance(variant.Output);
    if (!luminance) {
      m_Status = "Scene luminance measurement failed: " + luminance.error().Message;
      return;
    }
    completed.Variants.push_back(
        {.Render = std::move(variant), .Curve = std::move(*curve), .SceneLuminance = *luminance});
  }

  m_CompletedComparison = std::move(completed);
  m_Status = "Comparison complete: " + std::to_string(m_CompletedComparison->Variants.size()) + " variants.";
}

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void BrdfAnalysisPanel::Draw() {
  ImGui::Begin("BRDF Analysis", nullptr, ImGuiWindowFlags_NoCollapse);
  if (m_TargetsDirty) {
    RefreshTargets();
  }

  if (ImGui::Button("Refresh materials")) {
    RefreshTargets();
  }
  if (m_Targets.empty()) {
    ImGui::TextDisabled("%s", m_Status.c_str());
  } else {
    std::vector<const char*> labels;
    labels.reserve(m_Targets.size());
    for (const Target& target : m_Targets) {
      labels.push_back(target.Label.c_str());
    }
    if (ImGui::BeginCombo("Material", labels.at(m_SelectedTarget))) {
      for (size_t index = 0; index < m_Targets.size(); ++index) {
        const bool selected = index == m_SelectedTarget;
        if (ImGui::Selectable(labels.at(index), selected)) {
          m_SelectedTarget = index;
          m_CompletedComparison.reset();
          m_Status = "Material selection changed. Run a new comparison.";
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    ImGui::TextUnformatted("Compare BRDF models");
    bool modelSelectionChanged = false;
    if (ImGui::BeginTable("BrdfModelSelection", 3)) {
      for (ModelSelection& selection : m_ModelSelections) {
        ImGui::TableNextColumn();
        modelSelectionChanged =
            ImGui::Checkbox(ModelName(selection.Model), &selection.Selected) || modelSelectionChanged;
      }
      ImGui::EndTable();
    }
    if (modelSelectionChanged) {
      m_CompletedComparison.reset();
      m_Status = "BRDF model selection changed. Run a new comparison.";
    }
    if (ImGui::Button("Run comparison")) {
      RunComparison();
    }
  }

  ImGui::Separator();
  ImGui::TextWrapped("%s", m_Status.c_str());
  if (m_CompletedComparison) {
    const CompletedComparison& comparison = *m_CompletedComparison;
    ImGui::SeparatorText("Captured inputs");
    ImGui::Text("Target: %s | entity id: %llu", comparison.TargetLabel.c_str(),
                static_cast<unsigned long long>(comparison.Snapshot.TargetEntityId));
    ImGui::Text("Render size: %u x %u", comparison.Snapshot.Width, comparison.Snapshot.Height);
    ImGui::Text("Material BRDF: %s", comparison.Snapshot.SourceMaterial.brdfModel.c_str());
    const auto& material = comparison.Snapshot.SourceMaterialProperties;
    ImGui::Text("Base color: %.3f, %.3f, %.3f | metallic: %.3f", material.BaseColor.x, material.BaseColor.y,
                material.BaseColor.z, material.Metallic);
    ImGui::Text("Roughness: %.3f | IOR: %.3f | GTR gamma: %.3f", material.Roughness, material.Ior, material.GtrGamma);

    ImGui::SeparatorText("Response curves (light angle 0 to 90 degrees)");
    DrawResponseChart(comparison.Variants);

    if (ImGui::Button("Export chart PNG") && m_Export) {
      const auto result = m_Export(comparison, false);
      if (!result) {
        m_Status = result.error().Message;
      } else if (*result) {
        m_Status = **result;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Export measurements JSON") && m_Export) {
      const auto result = m_Export(comparison, true);
      if (!result) {
        m_Status = result.error().Message;
      } else if (*result) {
        m_Status = **result;
      }
    }

    ImGui::SeparatorText("Measurements");
    if (ImGui::BeginTable("BrdfAnalysisMeasurements", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
      constexpr std::array<const char*, 6> headings = {"Model",          "Curve peak", "Curve mean",
                                                       "Curve integral", "Scene mean", "Scene peak"};
      for (const char* heading : headings) {
        ImGui::TableSetupColumn(heading);
      }
      ImGui::TableHeadersRow();
      for (const VariantResult& variant : comparison.Variants) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(ModelName(variant.Curve.Model));
        ImGui::TableNextColumn();
        ImGui::Text("%.6g", variant.Curve.Measurements.Peak);
        ImGui::TableNextColumn();
        ImGui::Text("%.6g", variant.Curve.Measurements.Mean);
        ImGui::TableNextColumn();
        ImGui::Text("%.6g", variant.Curve.Measurements.Integral);
        ImGui::TableNextColumn();
        ImGui::Text("%.6g", variant.SceneLuminance.Mean);
        ImGui::TableNextColumn();
        ImGui::Text("%.6g", variant.SceneLuminance.Peak);
      }
      ImGui::EndTable();
    }
  }
  ImGui::End();
}
// NOLINTEND(cppcoreguidelines-pro-type-vararg)

#include "panels/include/AnalyticalFramePanel.h"

#include "events/include/SceneChangedEvent.h"
#include "panels/include/BrdfAnalysisExport.h"
#include "panels/include/CameraPreviewImageLayout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>
#include <limits>
#include <string_view>
#include <utility>

namespace {
using Engine::Renderer::BrdfModel;

constexpr std::array<BrdfModel, 9> AllModels = {BrdfModel::Legacy,           BrdfModel::GGX,     BrdfModel::GTR,
                                                BrdfModel::Beckmann,         BrdfModel::Gauss,   BrdfModel::BlinnPhong,
                                                BrdfModel::AshikhminShirley, BrdfModel::Schlick, BrdfModel::Sawicki};

constexpr std::array<Engine::RenderAov, 5> AllAovs = {
    Engine::RenderAov::FinalColor, Engine::RenderAov::Depth, Engine::RenderAov::WorldNormal,
    Engine::RenderAov::SpecularLighting, Engine::RenderAov::DiffuseLighting};

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

auto AovName(Engine::RenderAov aov) -> const char* {
  switch (aov) {
  case Engine::RenderAov::FinalColor:
    return "Final color";
  case Engine::RenderAov::Depth:
    return "Depth";
  case Engine::RenderAov::WorldNormal:
    return "World normal";
  case Engine::RenderAov::SpecularLighting:
    return "Specular lighting";
  case Engine::RenderAov::DiffuseLighting:
    return "Diffuse lighting";
  }
  return "Unknown";
}

auto FindModelIndex(BrdfModel model) -> int {
  for (size_t index = 0; index < AllModels.size(); ++index) {
    if (AllModels[index] == model) {
      return static_cast<int>(index);
    }
  }
  return 0;
}

auto FindAovIndex(Engine::RenderAov aov) -> int {
  for (size_t index = 0; index < AllAovs.size(); ++index) {
    if (AllAovs[index] == aov) {
      return static_cast<int>(index);
    }
  }
  return 0;
}

template <typename T, typename NameFunction>
auto DrawEnumCombo(const char* label, T& value, const auto& values, NameFunction&& getName) -> bool {
  const int current = [&] {
    for (size_t index = 0; index < values.size(); ++index) {
      if (values[index] == value) {
        return static_cast<int>(index);
      }
    }
    return 0;
  }();
  bool changed = false;
  if (ImGui::BeginCombo(label, getName(values.at(static_cast<size_t>(current))))) {
    for (const T item : values) {
      const bool selected = value == item;
      if (ImGui::Selectable(getName(item), selected)) {
        value = item;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

} // namespace

AnalyticalFramePanel::AnalyticalFramePanel(TargetProvider targets, CameraProvider cameras,
                                           SnapshotFunction captureSnapshot, RenderFunction render,
                                           UploadFunction upload, TextureFunction texture,
                                           ClearTextureFunction clearTexture, ChoosePathFunction choosePath)
    : m_Upload(std::move(upload)), m_Texture(std::move(texture)), m_ClearTexture(std::move(clearTexture)),
      m_ChoosePath(std::move(choosePath)), m_GetTargets(std::move(targets)), m_GetCameras(std::move(cameras)),
      m_CreateSnapshot(std::move(captureSnapshot)), m_Render(std::move(render)) {
  m_ModelSelections.reserve(AllModels.size());
  for (const BrdfModel model : AllModels) {
    m_ModelSelections.push_back({.Model = model, .Selected = true});
  }
}

void AnalyticalFramePanel::SetSession(Editor::Panels::AnalyticalFrameSession session) {
  Editor::Panels::AnalyticalFrameViewState state;
  if (session.ViewportState.is_object() && !session.ViewportState.empty()) {
    auto decodedState = Editor::Panels::AnalyticalFrameViewModel::FromViewportJson(session.ViewportState);
    if (!decodedState) {
      m_Status = "Could not display Analytical Frame: invalid viewport state: " + decodedState.error().Message;
      return;
    }
    state = std::move(*decodedState);
  }
  Open();
  m_Session = std::move(session);
  m_ViewState = state;
  if (!m_Session->Variants.empty()) {
    const auto hasModel = [this](BrdfModel model) { return FindVariant(model) != nullptr; };
    if (!hasModel(m_ViewState.SelectedModel)) {
      m_ViewState.SelectedModel = m_Session->Variants.front().Render.Request.Model;
    }
    if (!hasModel(m_ViewState.CompareModelA)) {
      m_ViewState.CompareModelA = m_Session->Variants.front().Render.Request.Model;
    }
    if (!hasModel(m_ViewState.CompareModelB)) {
      m_ViewState.CompareModelB = m_Session->Variants.size() > 1 ? m_Session->Variants[1].Render.Request.Model
                                                                 : m_Session->Variants.front().Render.Request.Model;
    }
  }
  m_Status = "Opened detached Analytical Frame project.";
  if (m_ViewState.PinnedPixel && (m_ViewState.PinnedPixel->at(0) >= m_Session->Snapshot.Width ||
                                  m_ViewState.PinnedPixel->at(1) >= m_Session->Snapshot.Height)) {
    m_ViewState.PinnedPixel.reset();
    StoreViewportState();
  }
  m_ImageDirty = true;
}

void AnalyticalFramePanel::Close() {
  m_Open = false;
  CancelCapture();
  if (m_ClearTexture) {
    m_ClearTexture();
  }
  m_ImageWidth = 0;
  m_ImageHeight = 0;
  m_ImageDirty = true;
}

void AnalyticalFramePanel::OnEvent(Engine::Events::Event& event) {
  Engine::Events::EventDispatcher dispatcher(event);
  dispatcher.Dispatch<SceneChangedEvent>([this](const auto&) -> bool {
    CancelCapture();
    m_TargetsDirty = true;
    return false;
  });
}

void AnalyticalFramePanel::RefreshTargets() {
  m_TargetsDirty = false;
  m_Targets.clear();
  m_Cameras.clear();
  m_SelectedTarget = 0;
  m_SelectedCamera = 0;
  if (!m_GetTargets || !m_GetCameras) {
    m_Status = "Scene material and camera selection are unavailable.";
    return;
  }
  auto targets = m_GetTargets();
  if (!targets) {
    m_Status = targets.error().Message;
    return;
  }
  auto cameras = m_GetCameras();
  if (!cameras) {
    m_Status = cameras.error().Message;
    return;
  }
  m_Targets = std::move(*targets);
  m_Cameras = std::move(*cameras);
  m_Status = m_Targets.empty()   ? "The active scene has no analyzable materials."
             : m_Cameras.empty() ? "The active scene has no authored cameras."
                                 : "Select a target, authored camera and BRDF models, then capture.";
}

void AnalyticalFramePanel::StartCapture() {
  if (m_SelectedTarget >= m_Targets.size() || m_SelectedCamera >= m_Cameras.size()) {
    m_Status = "Choose an authored camera and material target before capture.";
    return;
  }
  if (!m_CreateSnapshot || !m_Render) {
    m_Status = "Analytical Frame capture services are unavailable.";
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

  Engine::Renderer::BrdfAnalysis::BrdfAnalysisOverrides overrides;
  if (m_OverrideResolution) {
    overrides.OutputResolution =
        Engine::Renderer::BrdfAnalysis::BrdfAnalysisOverrides::Resolution{.Width = m_OverrideWidth,
                                                                          .Height = m_OverrideHeight};
  }
  if (m_OverrideRoughness) {
    overrides.Roughness = m_Roughness;
  }
  if (m_OverrideGtrGamma) {
    overrides.GtrGamma = m_GtrGamma;
  }
  auto snapshot = m_CreateSnapshot(m_Targets.at(m_SelectedTarget), m_Cameras.at(m_SelectedCamera), overrides);
  if (!snapshot) {
    m_Status = "Scene capture failed: " + snapshot.error().Message;
    return;
  }
  if (snapshot->TargetEntityId != m_Targets.at(m_SelectedTarget).EntityId) {
    m_Status = "Scene capture returned a different material target than the selected target.";
    return;
  }
  snapshot->FrameOverrides = overrides;
  auto capture = Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSession::Create(*snapshot, models);
  if (!capture) {
    m_Status = "Capture could not start: " + capture.error().Message;
    return;
  }
  m_CapturedSnapshot = std::move(*snapshot);
  m_Capture = std::move(*capture);
  m_Status = "Rendering variant 0 of " + std::to_string(m_Capture->GetTotalCount()) + ".";
}

void AnalyticalFramePanel::AdvanceCapture() {
  if (!m_Capture || m_Capture->GetStatus() != Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSessionState::Ready) {
    return;
  }
  auto request = m_Capture->GetNextRequest();
  if (!request) {
    m_Status = "Capture failed: " + request.error().Message;
    m_Capture.reset();
    m_CapturedSnapshot.reset();
    return;
  }
  if (*request == nullptr) {
    CompleteCapture();
    return;
  }
  auto output = m_Render(**request);
  if (!output) {
    m_Capture->Cancel();
    m_Capture.reset();
    m_CapturedSnapshot.reset();
    m_Status = "Render failed; partial Analytical Frame results were discarded: " + output.error().Message;
    return;
  }
  auto completed = m_Capture->CompleteCurrent(std::move(*output));
  if (!completed) {
    m_Capture.reset();
    m_CapturedSnapshot.reset();
    m_Status =
        "Render output was rejected; partial Analytical Frame results were discarded: " + completed.error().Message;
    return;
  }
  if (m_Capture->GetStatus() == Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureSessionState::Completed) {
    CompleteCapture();
  } else {
    m_Status = "Rendered variant " + std::to_string(m_Capture->GetCompletedCount()) + " of " +
               std::to_string(m_Capture->GetTotalCount()) + ".";
  }
}

void AnalyticalFramePanel::CancelCapture() {
  if (!m_Capture) {
    return;
  }
  m_Capture->Cancel();
  m_Capture.reset();
  m_CapturedSnapshot.reset();
  m_Status = "Capture cancelled; partial Analytical Frame results were discarded.";
}

void AnalyticalFramePanel::CompleteCapture() {
  if (!m_Capture || !m_CapturedSnapshot) {
    return;
  }
  auto result = m_Capture->TakeResult();
  if (!result) {
    m_Status = "Capture did not complete: " + result.error().Message;
    m_Capture.reset();
    m_CapturedSnapshot.reset();
    return;
  }
  if (result->Status != Engine::Renderer::BrdfAnalysis::BrdfAnalysisCaptureResult::State::Completed) {
    m_Status = "Capture cancelled; partial Analytical Frame results were discarded.";
    m_Capture.reset();
    m_CapturedSnapshot.reset();
    return;
  }
  Editor::Panels::AnalyticalFrameSession session;
  session.Snapshot = std::move(*m_CapturedSnapshot);
  session.TargetLabel = m_Targets.at(m_SelectedTarget).Label;
  session.Variants.reserve(result->Variants.size());
  for (auto& render : result->Variants) {
    auto curve = Engine::Renderer::BrdfAnalysis::AnalyzeBrdfResponse(render.Request.Material, render.Request.Model);
    auto luminance = Engine::Renderer::BrdfAnalysis::MeasureSceneLuminance(render.Output);
    if (!curve || !luminance) {
      m_Status = "Capture measurement failed; complete render data was discarded.";
      m_Capture.reset();
      m_CapturedSnapshot.reset();
      return;
    }
    session.Variants.push_back({.Render = std::move(render), .Curve = std::move(*curve), .SceneLuminance = *luminance});
  }
  session.ViewportState = Editor::Panels::AnalyticalFrameViewModel::ToViewportJson(m_ViewState);
  session.PostProcessCallbackPresent = static_cast<bool>(session.Snapshot.PostProcess);
  SetSession(std::move(session));
  m_Status = "Analytical Frame captured: " + std::to_string(m_Session->Variants.size()) + " BRDF variants.";
  m_Capture.reset();
  m_CapturedSnapshot.reset();
}

auto AnalyticalFramePanel::FindVariant(BrdfModel model) const -> const Editor::Panels::AnalyticalFrameVariant* {
  if (!m_Session) {
    return nullptr;
  }
  const auto found = std::find_if(m_Session->Variants.begin(), m_Session->Variants.end(),
                                  [model](const auto& variant) { return variant.Render.Request.Model == model; });
  return found == m_Session->Variants.end() ? nullptr : &*found;
}

auto AnalyticalFramePanel::IsRelativeErrorCompatible() const -> bool {
  if (!m_Session || m_Session->PostProcessCallbackPresent || m_ViewState.SelectedAov != Engine::RenderAov::FinalColor) {
    return false;
  }
  const auto* a = FindVariant(m_ViewState.CompareModelA);
  const auto* b = FindVariant(m_ViewState.CompareModelB);
  return a != nullptr && b != nullptr && a->Render.Request.Width == b->Render.Request.Width &&
         a->Render.Request.Height == b->Render.Request.Height &&
         Editor::Panels::AnalyticalFrameViewModel::HasCompatibleLinearFinalColor(a->Render.Output, b->Render.Output);
}

auto AnalyticalFramePanel::BuildCompletedComparison() const -> std::optional<BrdfAnalysisPanel::CompletedComparison> {
  if (!m_Session) {
    return std::nullopt;
  }
  BrdfAnalysisPanel::CompletedComparison comparison;
  comparison.Snapshot = m_Session->Snapshot;
  comparison.TargetLabel = m_Session->TargetLabel;
  comparison.Variants.reserve(m_Session->Variants.size());
  for (const auto& variant : m_Session->Variants) {
    comparison.Variants.push_back(
        {.Render = variant.Render, .Curve = variant.Curve, .SceneLuminance = variant.SceneLuminance});
  }
  return comparison;
}

auto AnalyticalFramePanel::BuildCurrentImage() const -> Engine::Result<std::vector<uint8_t>> {
  if (!m_Session) {
    return std::unexpected(Engine::MakeError("No Analytical Frame is open."));
  }
  const auto* variantA = FindVariant(
      m_ViewState.Mode == Editor::Panels::AnalyticalFrameViewState::ComparisonMode::Single ? m_ViewState.SelectedModel
                                                                                           : m_ViewState.CompareModelA);
  if (variantA == nullptr) {
    return std::unexpected(Engine::MakeError("The selected BRDF variant is not present in this Analytical Frame."));
  }
  if (m_ViewState.Mode == Editor::Panels::AnalyticalFrameViewState::ComparisonMode::Single) {
    return Editor::Panels::AnalyticalFrameViewModel::BuildDisplayImage(variantA->Render.Output,
                                                                       m_ViewState.SelectedAov);
  }
  const auto* variantB = FindVariant(m_ViewState.CompareModelB);
  if (variantB == nullptr) {
    return std::unexpected(Engine::MakeError("The second BRDF variant is not present in this Analytical Frame."));
  }
  if (m_ViewState.Mode == Editor::Panels::AnalyticalFrameViewState::ComparisonMode::Wipe) {
    return Editor::Panels::AnalyticalFrameViewModel::BuildWipeImage(variantA->Render.Output, variantB->Render.Output,
                                                                    m_ViewState.SelectedAov, m_ViewState.WipePosition);
  }
  if (!IsRelativeErrorCompatible() || variantA->Render.Request.Width != variantB->Render.Request.Width ||
      variantA->Render.Request.Height != variantB->Render.Request.Height) {
    return std::unexpected(
        Engine::MakeError("Relative luminance error requires compatible linear final-color captures."));
  }
  return Editor::Panels::AnalyticalFrameViewModel::BuildRelativeLuminanceHeatmap(variantA->Render.Output,
                                                                                 variantB->Render.Output);
}

bool AnalyticalFramePanel::RefreshImage() {
  if (!m_ImageDirty) {
    return true;
  }
  m_ImageDirty = false;
  if (!m_Session) {
    m_ImageWidth = 0;
    m_ImageHeight = 0;
    return false;
  }
  auto image = BuildCurrentImage();
  if (!image) {
    m_Status = image.error().Message;
    return false;
  }
  if (!m_Upload || !m_Upload(m_Session->Snapshot.Width, m_Session->Snapshot.Height, *image)) {
    m_Status = "The captured image could not be uploaded to the static viewer.";
    return false;
  }
  m_ImageWidth = m_Session->Snapshot.Width;
  m_ImageHeight = m_Session->Snapshot.Height;
  return true;
}

void AnalyticalFramePanel::DrawCaptureControls() {
  if (m_TargetsDirty) {
    RefreshTargets();
  }
  if (!m_Targets.empty()) {
    if (ImGui::BeginCombo("Material target",
                          m_Targets.at((std::min)(m_SelectedTarget, m_Targets.size() - 1U)).Label.c_str())) {
      for (size_t index = 0; index < m_Targets.size(); ++index) {
        const bool selected = index == m_SelectedTarget;
        if (ImGui::Selectable(m_Targets[index].Label.c_str(), selected)) {
          m_SelectedTarget = index;
        }
      }
      ImGui::EndCombo();
    }
  }
  if (!m_Cameras.empty()) {
    if (ImGui::BeginCombo("Authored camera",
                          m_Cameras.at((std::min)(m_SelectedCamera, m_Cameras.size() - 1U)).Label.c_str())) {
      for (size_t index = 0; index < m_Cameras.size(); ++index) {
        const bool selected = index == m_SelectedCamera;
        if (ImGui::Selectable(m_Cameras[index].Label.c_str(), selected)) {
          m_SelectedCamera = index;
        }
      }
      ImGui::EndCombo();
    }
  }
  ImGui::TextUnformatted("BRDF models");
  int modelColumn = 0;
  for (auto& selection : m_ModelSelections) {
    ImGui::PushID(static_cast<int>(selection.Model));
    ImGui::Checkbox(ModelName(selection.Model), &selection.Selected);
    ImGui::PopID();
    if (++modelColumn % 3 != 0) {
      ImGui::SameLine();
    }
  }
  if (ImGui::Checkbox("Resolution override", &m_OverrideResolution)) {
    m_ImageDirty = true;
  }
  if (m_OverrideResolution) {
    int width = static_cast<int>((std::min)(m_OverrideWidth, static_cast<uint32_t>((std::numeric_limits<int>::max)())));
    int height =
        static_cast<int>((std::min)(m_OverrideHeight, static_cast<uint32_t>((std::numeric_limits<int>::max)())));
    ImGui::SetNextItemWidth(120.0F);
    ImGui::InputInt("Width", &width);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0F);
    ImGui::InputInt("Height", &height);
    m_OverrideWidth = static_cast<uint32_t>((std::max)(width, 1));
    m_OverrideHeight = static_cast<uint32_t>((std::max)(height, 1));
  }
  ImGui::Checkbox("Roughness override", &m_OverrideRoughness);
  if (m_OverrideRoughness) {
    ImGui::SliderFloat("Roughness", &m_Roughness, 0.0F, 1.0F);
  }
  ImGui::Checkbox("GTR gamma override", &m_OverrideGtrGamma);
  if (m_OverrideGtrGamma) {
    ImGui::SliderFloat("GTR gamma", &m_GtrGamma, 0.05F, 16.0F);
  }
  if (!m_Capture) {
    if (ImGui::Button("Capture Analytical Frame")) {
      StartCapture();
    }
  } else {
    if (ImGui::Button("Cancel capture")) {
      CancelCapture();
    }
    ImGui::SameLine();
    ImGui::ProgressBar(static_cast<float>(m_Capture->GetCompletedCount()) /
                           static_cast<float>((std::max)(m_Capture->GetTotalCount(), size_t{1})),
                       ImVec2(200.0F, 0.0F));
  }
  ImGui::SameLine();
  ImGui::TextUnformatted(m_Status.c_str());
}

void AnalyticalFramePanel::DrawControls() {
  if (!m_Session) {
    ImGui::TextUnformatted("No captured frame is open.");
    return;
  }
  bool imageChanged = false;
  imageChanged |= DrawEnumCombo("AOV", m_ViewState.SelectedAov, AllAovs, AovName);
  std::vector<BrdfModel> availableModels;
  availableModels.reserve(m_Session->Variants.size());
  for (const auto& variant : m_Session->Variants) {
    availableModels.push_back(variant.Render.Request.Model);
  }
  if (m_ViewState.Mode == Editor::Panels::AnalyticalFrameViewState::ComparisonMode::Single) {
    imageChanged |= DrawEnumCombo("BRDF model", m_ViewState.SelectedModel, availableModels, ModelName);
  } else {
    imageChanged |= DrawEnumCombo("Model A", m_ViewState.CompareModelA, availableModels, ModelName);
    ImGui::SameLine();
    imageChanged |= DrawEnumCombo("Model B", m_ViewState.CompareModelB, availableModels, ModelName);
  }
  std::vector<Editor::Panels::AnalyticalFrameViewState::ComparisonMode> modes = {
      Editor::Panels::AnalyticalFrameViewState::ComparisonMode::Single,
      Editor::Panels::AnalyticalFrameViewState::ComparisonMode::Wipe};
  const bool relativeErrorCompatible = IsRelativeErrorCompatible();
  if (m_ViewState.Mode == Editor::Panels::AnalyticalFrameViewState::ComparisonMode::RelativeLuminanceError &&
      !relativeErrorCompatible) {
    m_ViewState.Mode = Editor::Panels::AnalyticalFrameViewState::ComparisonMode::Single;
    imageChanged = true;
    m_Status = "Relative luminance error is unavailable for these AOVs or capture settings.";
  }
  if (relativeErrorCompatible) {
    modes.push_back(Editor::Panels::AnalyticalFrameViewState::ComparisonMode::RelativeLuminanceError);
  }
  auto modeName = [](Editor::Panels::AnalyticalFrameViewState::ComparisonMode mode) -> const char* {
    switch (mode) {
    case Editor::Panels::AnalyticalFrameViewState::ComparisonMode::Single:
      return "Single";
    case Editor::Panels::AnalyticalFrameViewState::ComparisonMode::Wipe:
      return "A/B wipe";
    case Editor::Panels::AnalyticalFrameViewState::ComparisonMode::RelativeLuminanceError:
      return "Relative luminance error";
    }
    return "Single";
  };
  if (DrawEnumCombo("View mode", m_ViewState.Mode, modes, modeName)) {
    imageChanged = true;
  }
  if (m_ViewState.Mode == Editor::Panels::AnalyticalFrameViewState::ComparisonMode::Wipe) {
    imageChanged |= ImGui::SliderFloat("Wipe position", &m_ViewState.WipePosition, 0.0F, 1.0F);
  }
  if (ImGui::Button("Fit image")) {
    m_ViewState.Zoom = 1.0F;
    m_ViewState.PanOffsetPixels = {};
  }
  if (ImGui::Button("Open .aframe")) {
    OpenArchive();
  }
  ImGui::SameLine();
  if (ImGui::Button("Save .aframe")) {
    SaveArchive();
  }
  ImGui::SameLine();
  if (ImGui::Button("Export curve PNG")) {
    Export(true);
  }
  ImGui::SameLine();
  if (ImGui::Button("Export measurements JSON")) {
    Export(false);
  }
  if (imageChanged) {
    m_ImageDirty = true;
  }
  StoreViewportState();
}

void AnalyticalFramePanel::DrawMeasurements() const {
  if (!m_Session) {
    return;
  }
  const auto* selected = FindVariant(m_ViewState.SelectedModel);
  if (selected == nullptr && !m_Session->Variants.empty()) {
    selected = &m_Session->Variants.front();
  }
  if (selected != nullptr) {
    DrawCurveChart();
    ImGui::SeparatorText("Selected variant measurements");
    ImGui::Text("BRDF: %s", ModelName(selected->Curve.Model));
    ImGui::Text("Curve peak %.6g | mean %.6g | integral %.6g", selected->Curve.Measurements.Peak,
                selected->Curve.Measurements.Mean, selected->Curve.Measurements.Integral);
    ImGui::Text("Scene luminance mean %.6g | peak %.6g", selected->SceneLuminance.Mean, selected->SceneLuminance.Peak);
  }
  if (!m_ViewState.PinnedPixel) {
    return;
  }
  const uint32_t x = m_ViewState.PinnedPixel->at(0);
  const uint32_t y = m_ViewState.PinnedPixel->at(1);
  ImGui::SeparatorText("Pinned pixel");
  ImGui::Text("(%u, %u) | %s", x, y, AovName(m_ViewState.SelectedAov));
  const auto printSample = [this, x, y](BrdfModel model, const char* prefix) {
    const auto* variant = FindVariant(model);
    if (variant == nullptr) {
      return;
    }
    const auto sample =
        Editor::Panels::AnalyticalFrameViewModel::ReadPixel(variant->Render.Output, m_ViewState.SelectedAov, x, y);
    if (!sample) {
      return;
    }
    ImGui::Text("%s %s:", prefix, ModelName(model));
    for (uint32_t channel = 0; channel < sample->ChannelCount; ++channel) {
      ImGui::SameLine();
      ImGui::Text("%s %.6g", Engine::RenderOutput::GetDescriptor(m_ViewState.SelectedAov).ChannelNames[channel],
                  sample->Channels[channel]);
    }
  };
  if (m_ViewState.Mode == Editor::Panels::AnalyticalFrameViewState::ComparisonMode::Single) {
    printSample(m_ViewState.SelectedModel, "Value");
  } else {
    printSample(m_ViewState.CompareModelA, "A");
    printSample(m_ViewState.CompareModelB, "B");
    if (IsRelativeErrorCompatible()) {
      const auto* a = FindVariant(m_ViewState.CompareModelA);
      const auto* b = FindVariant(m_ViewState.CompareModelB);
      if (a != nullptr && b != nullptr) {
        auto errors =
            Editor::Panels::AnalyticalFrameViewModel::ComputeRelativeLuminanceError(a->Render.Output, b->Render.Output);
        if (errors && x < a->Render.Output.Width && y < a->Render.Output.Height) {
          ImGui::Text("Raw relative luminance error: %.8g",
                      errors->at(static_cast<size_t>(y) * a->Render.Output.Width + x));
        }
      }
    }
  }
}

void AnalyticalFramePanel::DrawCurveChart() const {
  if (!m_Session || m_Session->Variants.empty()) {
    return;
  }
  constexpr float chartHeight = 150.0F;
  constexpr float leftPadding = 34.0F;
  constexpr float rightPadding = 8.0F;
  const float width = (std::max)(ImGui::GetContentRegionAvail().x, 180.0F);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##AnalyticalFrameCurveChart", ImVec2(width, chartHeight));
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImVec2 plotMinimum(origin.x + leftPadding, origin.y + 8.0F);
  const ImVec2 plotMaximum(origin.x + width - rightPadding, origin.y + chartHeight - 18.0F);
  drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + chartHeight), IM_COL32(22, 25, 30, 255));
  drawList->AddRect(plotMinimum, plotMaximum, IM_COL32(105, 112, 120, 255));
  float responseMaximum = 0.0F;
  for (const auto& variant : m_Session->Variants) {
    responseMaximum = (std::max)(responseMaximum, variant.Curve.Measurements.Peak);
  }
  responseMaximum = responseMaximum > 0.0F ? responseMaximum : 1.0F;
  const auto colorFor = [](size_t index) -> ImU32 {
    constexpr std::array<ImU32, 9> colors = {
        IM_COL32(255, 196, 64, 255),  IM_COL32(77, 180, 255, 255),  IM_COL32(140, 225, 140, 255),
        IM_COL32(229, 117, 128, 255), IM_COL32(192, 128, 255, 255), IM_COL32(255, 155, 64, 255),
        IM_COL32(128, 220, 210, 255), IM_COL32(255, 128, 200, 255), IM_COL32(180, 190, 100, 255)};
    return colors[index % colors.size()];
  };
  for (size_t variantIndex = 0; variantIndex < m_Session->Variants.size(); ++variantIndex) {
    const auto& curve = m_Session->Variants.at(variantIndex).Curve;
    if (curve.AnglesDegrees.size() != curve.Response.size() || curve.Response.size() < 2) {
      continue;
    }
    std::vector<ImVec2> points;
    points.reserve(curve.Response.size());
    for (size_t sample = 0; sample < curve.Response.size(); ++sample) {
      const float xFraction = curve.AnglesDegrees.at(sample) / 90.0F;
      const float yFraction = curve.Response.at(sample) / responseMaximum;
      points.emplace_back(plotMinimum.x + xFraction * (plotMaximum.x - plotMinimum.x),
                          plotMaximum.y - yFraction * (plotMaximum.y - plotMinimum.y));
    }
    drawList->AddPolyline(points.data(), static_cast<int>(points.size()), colorFor(variantIndex), ImDrawFlags_None,
                          1.5F);
  }
  drawList->AddText(ImVec2(plotMinimum.x, plotMaximum.y + 3.0F), IM_COL32(190, 195, 202, 255), "0");
  drawList->AddText(ImVec2(plotMaximum.x - 16.0F, plotMaximum.y + 3.0F), IM_COL32(190, 195, 202, 255), "90 deg");
}

void AnalyticalFramePanel::DrawCanvas() {
  if (!m_Session) {
    return;
  }
  const bool hasImage = RefreshImage();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const ImVec2 canvasPosition = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##AnalyticalFrameCanvas", available,
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImVec2 canvasMaximum(canvasPosition.x + available.x, canvasPosition.y + available.y);
  drawList->AddRectFilled(canvasPosition, canvasMaximum, IM_COL32(20, 22, 26, 255));
  void* texture = m_Texture ? m_Texture() : nullptr;
  if (!hasImage || m_ImageWidth == 0 || m_ImageHeight == 0 || texture == nullptr) {
    drawList->AddText(ImVec2(canvasPosition.x + 12.0F, canvasPosition.y + 12.0F), IM_COL32(210, 210, 210, 255),
                      m_Status.c_str());
    return;
  }
  const float fitScale =
      CameraPreviewImageLayout::ComputeFitScale(m_ImageWidth, m_ImageHeight, available.x, available.y);
  const float scale = fitScale * m_ViewState.Zoom;
  const float imageWidth = static_cast<float>(m_ImageWidth) * scale;
  const float imageHeight = static_cast<float>(m_ImageHeight) * scale;
  const float imageX =
      canvasPosition.x + (std::max)(0.0F, (available.x - imageWidth) * 0.5F) + m_ViewState.PanOffsetPixels[0];
  const float imageY =
      canvasPosition.y + (std::max)(0.0F, (available.y - imageHeight) * 0.5F) + m_ViewState.PanOffsetPixels[1];
  const ImVec2 imageMinimum(imageX, imageY);
  const ImVec2 imageMaximum(imageX + imageWidth, imageY + imageHeight);
  drawList->PushClipRect(canvasPosition, canvasMaximum, true);
  drawList->AddImage(texture, imageMinimum, imageMaximum);
  if (m_ViewState.Mode == Editor::Panels::AnalyticalFrameViewState::ComparisonMode::Wipe) {
    const float wipeX = imageX + imageWidth * m_ViewState.WipePosition;
    drawList->AddLine(ImVec2(wipeX, imageY), ImVec2(wipeX, imageY + imageHeight), IM_COL32(255, 255, 255, 255), 2.0F);
  }
  if (m_ViewState.PinnedPixel) {
    const float pinX = imageX + (static_cast<float>(m_ViewState.PinnedPixel->at(0)) + 0.5F) * scale;
    const float pinY = imageY + (static_cast<float>(m_ViewState.PinnedPixel->at(1)) + 0.5F) * scale;
    drawList->AddCircle(ImVec2(pinX, pinY), 5.0F, IM_COL32(255, 255, 255, 255), 0, 2.0F);
  }
  drawList->PopClipRect();

  if (hovered && ImGui::GetIO().MouseWheel != 0.0F) {
    m_ViewState.Zoom = std::clamp(m_ViewState.Zoom * std::pow(1.15F, ImGui::GetIO().MouseWheel), 0.05F, 32.0F);
  }
  if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    m_ViewState.PanOffsetPixels[0] += delta.x;
    m_ViewState.PanOffsetPixels[1] += delta.y;
  }
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    UpdatePinnedPixel(mouse.x, mouse.y, imageX, imageY, scale);
  }
  StoreViewportState();
}

void AnalyticalFramePanel::UpdatePinnedPixel(float mouseX, float mouseY, float imageX, float imageY, float scale) {
  if (scale <= 0.0F || mouseX < imageX || mouseY < imageY) {
    return;
  }
  const uint32_t x = static_cast<uint32_t>((mouseX - imageX) / scale);
  const uint32_t y = static_cast<uint32_t>((mouseY - imageY) / scale);
  if (x >= m_ImageWidth || y >= m_ImageHeight) {
    return;
  }
  m_ViewState.PinnedPixel = std::array<uint32_t, 2>{x, y};
  StoreViewportState();
}

void AnalyticalFramePanel::StoreViewportState() {
  if (m_Session) {
    m_Session->ViewportState = Editor::Panels::AnalyticalFrameViewModel::ToViewportJson(m_ViewState);
  }
}

void AnalyticalFramePanel::OpenArchive() {
  if (!m_ChoosePath) {
    m_Status = "Archive file dialogs are unavailable.";
    return;
  }
  auto path = m_ChoosePath(FileOperation::OpenArchive, std::filesystem::current_path() / "analysis.aframe");
  if (!path) {
    return;
  }
  auto loaded = Editor::Panels::AnalyticalFrameArchive::Load(*path);
  if (!loaded) {
    m_Status = "Could not open Analytical Frame project: " + loaded.error().Message;
    return;
  }
  SetSession(std::move(*loaded));
}

void AnalyticalFramePanel::SaveArchive() {
  if (!m_Session || !m_ChoosePath) {
    m_Status = "There is no complete Analytical Frame to save.";
    return;
  }
  auto path = m_ChoosePath(FileOperation::SaveArchive, std::filesystem::current_path() / "analysis.aframe");
  if (!path) {
    return;
  }
  StoreViewportState();
  const Engine::Status saved = Editor::Panels::AnalyticalFrameArchive::Save(*path, *m_Session);
  m_Status =
      saved ? "Analytical Frame project saved." : "Could not save Analytical Frame project: " + saved.error().Message;
}

void AnalyticalFramePanel::Export(bool chart) {
  if (!m_Session || !m_ChoosePath) {
    m_Status = "There is no complete Analytical Frame to export.";
    return;
  }
  const auto operation = chart ? FileOperation::ExportChart : FileOperation::ExportMeasurements;
  auto path =
      m_ChoosePath(operation, std::filesystem::current_path() / (chart ? "brdf-analysis.png" : "brdf-analysis.json"));
  if (!path) {
    return;
  }
  auto comparison = BuildCompletedComparison();
  if (!comparison) {
    m_Status = "The displayed frame has no exportable measurements.";
    return;
  }
  const Engine::Status status = chart ? BrdfAnalysisExporter::WriteChartPng(*comparison, *path)
                                      : BrdfAnalysisExporter::WriteJson(*comparison, *path);
  m_Status =
      status ? "Analytical Frame export completed." : "Analytical Frame export failed: " + status.error().Message;
}

void AnalyticalFramePanel::Draw() {
  if (!m_Open) {
    return;
  }
  if (m_TargetsDirty && !m_Capture) {
    RefreshTargets();
  }
  const bool visible = ImGui::Begin("Analytical Frame", &m_Open);
  if (visible) {
    DrawCaptureControls();
    if (m_Session) {
      DrawControls();
      DrawCanvas();
      DrawMeasurements();
    }
  }
  ImGui::End();
  if (!m_Open) {
    Close();
    return;
  }
  AdvanceCapture();
}

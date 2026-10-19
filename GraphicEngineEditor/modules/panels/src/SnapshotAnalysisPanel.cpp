#include "panels/include/SnapshotAnalysisPanel.h"

#include "panels/include/CameraPreviewImageLayout.h"
#include "rendering/include/analysis/SnapshotAnalysis.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <imgui.h>
#include <limits>

namespace {

constexpr std::array<Engine::SnapshotPlane, 7> SnapshotPlanes = {
    Engine::SnapshotPlane::FinalColor,      Engine::SnapshotPlane::Depth,
    Engine::SnapshotPlane::WorldNormal,     Engine::SnapshotPlane::SpecularLighting,
    Engine::SnapshotPlane::DiffuseLighting, Engine::SnapshotPlane::MaterialInstanceId,
    Engine::SnapshotPlane::Overdraw};
constexpr std::array<const char*, 7> SnapshotPlaneNames = {
    "final_color",          "depth",   "world_normal", "specular_lighting", "diffuse_lighting",
    "material_instance_id", "overdraw"};

auto GetSnapshotPlaneName(Engine::SnapshotPlane plane) -> const char* {
  const size_t index = static_cast<size_t>(plane);
  return index < SnapshotPlaneNames.size() ? SnapshotPlaneNames[index] : "unknown";
}

auto Encode(float sample) -> uint8_t {
  const float safe = std::isfinite(sample) ? (std::max)(sample, 0.0F) : 0.0F;
  const float mapped = safe / (1.0F + safe);
  const float srgb = mapped <= 0.0031308F ? mapped * 12.92F : 1.055F * std::pow(mapped, 1.0F / 2.4F) - 0.055F;
  return static_cast<uint8_t>(std::lround(std::clamp(srgb, 0.0F, 1.0F) * 255.0F));
}

auto HashColor(uint32_t value) -> std::array<float, 3> {
  uint32_t hash = value + 0x9e3779b9U;
  hash ^= hash >> 16U;
  hash *= 0x7feb352dU;
  hash ^= hash >> 15U;
  hash *= 0x846ca68bU;
  hash ^= hash >> 16U;
  return {0.18F + static_cast<float>((hash >> 0U) & 0xffU) / 320.0F,
          0.18F + static_cast<float>((hash >> 8U) & 0xffU) / 320.0F,
          0.18F + static_cast<float>((hash >> 16U) & 0xffU) / 320.0F};
}

auto FindPlane(const Engine::SnapshotFrame& frame, Engine::RenderAov aov) -> const Engine::RenderOutputPlane* {
  for (const auto& plane : frame.GetFloatPlanes()) {
    if (plane.Aov == aov) {
      return &plane;
    }
  }
  return nullptr;
}

auto ToRenderAov(Engine::SnapshotPlane plane) -> std::optional<Engine::RenderAov> {
  switch (plane) {
  case Engine::SnapshotPlane::FinalColor:
    return Engine::RenderAov::FinalColor;
  case Engine::SnapshotPlane::Depth:
    return Engine::RenderAov::Depth;
  case Engine::SnapshotPlane::WorldNormal:
    return Engine::RenderAov::WorldNormal;
  case Engine::SnapshotPlane::SpecularLighting:
    return Engine::RenderAov::SpecularLighting;
  case Engine::SnapshotPlane::DiffuseLighting:
    return Engine::RenderAov::DiffuseLighting;
  case Engine::SnapshotPlane::MaterialInstanceId:
  case Engine::SnapshotPlane::Overdraw:
    return std::nullopt;
  }
  return std::nullopt;
}

auto GetDisplayRgb(const Engine::SnapshotFrame& frame, Engine::SnapshotPlane selectedPlane, size_t pixel)
    -> std::optional<std::array<float, 3>> {
  if (const auto aov = ToRenderAov(selectedPlane); aov) {
    const auto* plane = FindPlane(frame, *aov);
    if (plane == nullptr || pixel >= static_cast<size_t>(frame.GetWidth()) * frame.GetHeight()) {
      return std::nullopt;
    }
    const size_t base = pixel * plane->ChannelCount;
    if (*aov == Engine::RenderAov::Depth) {
      const float depth = plane->Pixels[base];
      return std::array<float, 3>{depth, depth, depth};
    }
    std::array<float, 3> rgb{};
    for (size_t channel = 0; channel < (std::min)(3U, plane->ChannelCount); ++channel) {
      const float sample = plane->Pixels[base + channel];
      rgb[channel] = *aov == Engine::RenderAov::WorldNormal ? 0.5F * sample + 0.5F : sample;
    }
    return rgb;
  }
  if (selectedPlane == Engine::SnapshotPlane::MaterialInstanceId) {
    if (!frame.HasPlane(selectedPlane) || pixel >= frame.GetMaterialInstanceIds().size()) {
      return std::nullopt;
    }
    const uint32_t id = frame.GetMaterialInstanceIds()[pixel];
    return id == 0U ? std::array<float, 3>{0.0F, 0.0F, 0.0F} : HashColor(id);
  }
  if (!frame.HasPlane(Engine::SnapshotPlane::Overdraw) || pixel >= frame.GetOverdraw().size()) {
    return std::nullopt;
  }
  const float value = std::log2(static_cast<float>(frame.GetOverdraw()[pixel]) + 1.0F) / 8.0F;
  return std::array<float, 3>{value, value * value, 1.0F - value};
}

auto ToAnalysisView(const Engine::SnapshotFrame& frame, Engine::SnapshotPlane selectedPlane)
    -> Engine::SnapshotAnalysis::PlaneView {
  using namespace Engine::SnapshotAnalysis;
  if (selectedPlane == Engine::SnapshotPlane::MaterialInstanceId) {
    return {frame.GetWidth(),
            frame.GetHeight(),
            1U,
            PlaneSemantic::MaterialInstanceId,
            ScalarFormat::UInt32,
            {},
            {},
            frame.GetMaterialInstanceIds()};
  }
  if (selectedPlane == Engine::SnapshotPlane::Overdraw) {
    return {frame.GetWidth(),   frame.GetHeight(), 1U, PlaneSemantic::Overdraw, ScalarFormat::UInt32, {}, {},
            frame.GetOverdraw()};
  }
  const auto aov = ToRenderAov(selectedPlane);
  if (!aov) {
    return {};
  }
  const Engine::RenderOutputPlane* plane = FindPlane(frame, *aov);
  if (plane == nullptr) {
    return {};
  }
  PlaneSemantic semantic = PlaneSemantic::FinalColor;
  switch (*aov) {
  case Engine::RenderAov::FinalColor:
    semantic = PlaneSemantic::FinalColor;
    break;
  case Engine::RenderAov::Depth:
    semantic = PlaneSemantic::Depth;
    break;
  case Engine::RenderAov::WorldNormal:
    semantic = PlaneSemantic::WorldNormal;
    break;
  case Engine::RenderAov::SpecularLighting:
    semantic = PlaneSemantic::SpecularLighting;
    break;
  case Engine::RenderAov::DiffuseLighting:
    semantic = PlaneSemantic::DiffuseLighting;
    break;
  }
  ColorInterpretation color;
  color.Primaries = ColorPrimaries::Rec709;
  color.Transfer = TransferFunction::Linear;
  color.NitsPerSceneLinearUnit = frame.GetMetadata().Color.NitsPerSceneUnit
                                     ? std::optional<double>(*frame.GetMetadata().Color.NitsPerSceneUnit)
                                     : std::nullopt;
  return {frame.GetWidth(),
          frame.GetHeight(),
          static_cast<uint8_t>(plane->ChannelCount),
          semantic,
          ScalarFormat::Float32,
          color,
          plane->Pixels,
          {}};
}

} // namespace

SnapshotAnalysisPanel::SnapshotAnalysisPanel(BeginCaptureFunction beginCapture, ListCamerasFunction listCameras,
                                             PollCaptureFunction pollCapture, CaptureStatusFunction getCaptureStatus,
                                             CancelCaptureFunction cancelCapture, UploadImageFunction uploadImage,
                                             TextureFunction getTexture, ClearImageFunction clearImage,
                                             WorkspaceServices workspaceServices)
    : m_BeginCapture(std::move(beginCapture)), m_ListCameras(std::move(listCameras)),
      m_PollCapture(std::move(pollCapture)), m_GetCaptureStatus(std::move(getCaptureStatus)),
      m_CancelCapture(std::move(cancelCapture)), m_UploadImage(std::move(uploadImage)),
      m_GetTexture(std::move(getTexture)), m_ClearImage(std::move(clearImage)),
      m_WorkspaceServices(std::move(workspaceServices)) {}

void SnapshotAnalysisPanel::StartCapture() {
  if (m_CaptureTicket || !m_BeginCapture) {
    return;
  }
  if (!HasSnapshotCapturePlaneSelection(m_SelectedCapturePlanes)) {
    m_CaptureMessage = "Capture requires at least one selected plane.";
    return;
  }
  if (!m_SelectedCamera || std::ranges::none_of(m_CameraOptions, [this](const auto& camera) {
        return camera.Entity == *m_SelectedCamera;
      })) {
    m_CaptureMessage = "Select a valid authored scene camera before capturing.";
    return;
  }
  auto ticket = m_BeginCapture(*m_SelectedCamera, m_WidthOverride, m_HeightOverride, m_SelectedCapturePlanes);
  if (!ticket) {
    m_CaptureMessage = "Capture failed to start: " + ticket.error().Message;
    return;
  }
  m_CaptureTicket = *ticket;
  m_CaptureMessage = "Capture submitted. Waiting for GPU readback...";
}

void SnapshotAnalysisPanel::RefreshCameraOptions() {
  m_CameraOptions = m_ListCameras ? m_ListCameras() : std::vector<SnapshotAnalysisCameraOption>{};
  if (m_SelectedCamera &&
      std::ranges::any_of(m_CameraOptions, [this](const auto& camera) { return camera.Entity == *m_SelectedCamera; })) {
    return;
  }
  m_SelectedCamera = SelectPreferredSnapshotCamera(m_CameraOptions);
}

void SnapshotAnalysisPanel::UpdateCapture() {
  if (!m_CaptureTicket || !m_PollCapture) {
    return;
  }
  auto result = m_PollCapture(*m_CaptureTicket);
  if (!result) {
    m_CaptureMessage = "Capture failed: " + result.error().Message;
    m_CaptureTicket.reset();
    return;
  }
  if (*result) {
    auto tab =
        std::make_unique<Tab>(Tab{.Name = "Frame " + std::to_string(m_Tabs.size() + 1U), .Frame = std::move(**result)});
    tab->SelectedPlane = SelectAvailableSnapshotPlane(tab->Frame, Engine::SnapshotPlane::FinalColor)
                             .value_or(Engine::SnapshotPlane::FinalColor);
    m_Tabs.push_back(std::move(tab));
    m_ActiveTab = m_Tabs.size() - 1U;
    m_CaptureMessage = "Captured immutable frame " + std::to_string(m_Tabs.size()) + " (" +
                       std::to_string(m_Tabs.back()->Frame.GetWidth()) + " × " +
                       std::to_string(m_Tabs.back()->Frame.GetHeight()) + ").";
    m_CaptureTicket.reset();
    m_DisplayDirty = true;
    return;
  }
  if (m_GetCaptureStatus) {
    const auto status = m_GetCaptureStatus(*m_CaptureTicket);
    if (status) {
      m_CaptureMessage = status->Message;
      if (status->CurrentPhase == Engine::RenderSystem::SnapshotRenderStatus::Phase::Failed ||
          status->CurrentPhase == Engine::RenderSystem::SnapshotRenderStatus::Phase::Canceled) {
        m_CaptureTicket.reset();
      }
    }
  }
}

void SnapshotAnalysisPanel::OpenExr() {
  if (!m_WorkspaceServices.ChoosePath || !m_WorkspaceServices.ReadExr) {
    m_CaptureMessage = "EXR open is unavailable.";
    return;
  }
  const auto path = m_WorkspaceServices.ChoosePath(true, false, m_WorkspacePath);
  if (!path) {
    return;
  }
  auto frame = m_WorkspaceServices.ReadExr(*path);
  if (!frame) {
    m_CaptureMessage = "Could not open snapshot EXR: " + frame.error().Message;
    return;
  }
  auto tab = std::make_unique<Tab>(Tab{.Name = path->stem().string(), .Frame = std::move(*frame), .ExrPath = *path});
  tab->SelectedPlane = SelectAvailableSnapshotPlane(tab->Frame, Engine::SnapshotPlane::FinalColor)
                           .value_or(Engine::SnapshotPlane::FinalColor);
  m_Tabs.push_back(std::move(tab));
  m_ActiveTab = m_Tabs.size() - 1U;
  m_DisplayDirty = true;
  m_FocusRequested = true;
  m_CaptureMessage = "Opened immutable snapshot frame from " + path->string();
}

void SnapshotAnalysisPanel::OpenWorkspace() {
  if (!m_WorkspaceServices.ChoosePath || !m_WorkspaceServices.LoadWorkspace) {
    m_CaptureMessage = "Workspace open is unavailable.";
    return;
  }
  const auto path = m_WorkspaceServices.ChoosePath(false, false, m_WorkspacePath);
  if (!path) {
    return;
  }
  auto loaded = m_WorkspaceServices.LoadWorkspace(*path);
  if (!loaded) {
    m_CaptureMessage = "Could not open workspace: " + loaded.error().Message;
    return;
  }
  if (loaded->Frames.size() != loaded->Document.Tabs.size() ||
      loaded->Frames.size() != loaded->ResolvedExrPaths.size()) {
    m_CaptureMessage = "Could not open workspace: loaded tab data is incomplete.";
    return;
  }
  for (size_t index = 0; index < loaded->Frames.size(); ++index) {
    if (!loaded->Frames[index].HasPlane(loaded->Document.Tabs[index].SelectedPlane)) {
      m_CaptureMessage = "Could not open workspace: tab " + std::to_string(index + 1U) + " selects plane '" +
                         GetSnapshotPlaneName(loaded->Document.Tabs[index].SelectedPlane) +
                         "' which its EXR payload does not contain.";
      return;
    }
  }
  const size_t offset = m_Tabs.size();
  m_ComparisonReference.reset();
  m_ShowWipe = false;
  for (size_t index = 0; index < loaded->Frames.size(); ++index) {
    auto& state = loaded->Document.Tabs[index];
    auto tab = std::make_unique<Tab>(Tab{.Name = loaded->ResolvedExrPaths[index].stem().string(),
                                         .Frame = std::move(loaded->Frames[index]),
                                         .ExrPath = loaded->ResolvedExrPaths[index],
                                         .SelectedPlane = state.SelectedPlane,
                                         .Zoom = state.Zoom,
                                         .Pan = {state.PanX, state.PanY}});
    if (state.PinnedPixel) {
      tab->PinnedPixel = std::array<uint32_t, 2>{state.PinnedPixel->X, state.PinnedPixel->Y};
    }
    if (state.Roi) {
      tab->Roi = std::array<uint32_t, 4>{state.Roi->X, state.Roi->Y, state.Roi->X + state.Roi->Width,
                                         state.Roi->Y + state.Roi->Height};
    }
    m_Tabs.push_back(std::move(tab));
  }
  if (!loaded->Frames.empty()) {
    m_ActiveTab = offset + loaded->Document.SelectedTab;
    if (loaded->Document.Comparison) {
      const auto& comparison = *loaded->Document.Comparison;
      m_ComparisonReference = offset + comparison.ReferenceTab;
      m_ActiveTab = offset + comparison.CurrentTab;
      m_WipePosition = comparison.WipePosition;
      m_ShowWipe = true;
      switch (comparison.Metric) {
      case Engine::SnapshotPersistence::SnapshotComparisonMetric::AbsoluteError:
        m_ComparisonMetric = Engine::SnapshotAnalysis::DifferenceMetric::MeanAbsolute;
        break;
      case Engine::SnapshotPersistence::SnapshotComparisonMetric::RgbRms:
        m_ComparisonMetric = Engine::SnapshotAnalysis::DifferenceMetric::RgbRms;
        break;
      case Engine::SnapshotPersistence::SnapshotComparisonMetric::RelativeLuminance:
        m_ComparisonMetric = Engine::SnapshotAnalysis::DifferenceMetric::RelativeLuminance;
        break;
      }
    }
  }
  m_WorkspacePath = *path;
  m_DisplayDirty = true;
  m_FocusRequested = true;
  m_CaptureMessage = "Opened workspace and validated all referenced EXR payloads.";
}

void SnapshotAnalysisPanel::SaveCurrentFrame() {
  if (!m_ActiveTab || *m_ActiveTab >= m_Tabs.size() || !m_WorkspaceServices.ChoosePath ||
      !m_WorkspaceServices.WriteExr) {
    m_CaptureMessage = "Select a frame tab before exporting an EXR.";
    return;
  }
  const auto path = m_WorkspaceServices.ChoosePath(true, true, m_Tabs[*m_ActiveTab]->ExrPath);
  if (!path) {
    return;
  }
  const auto result = m_WorkspaceServices.WriteExr(m_Tabs[*m_ActiveTab]->Frame, *path);
  if (!result) {
    m_CaptureMessage = "EXR export failed: " + result.error().Message;
    return;
  }
  m_Tabs[*m_ActiveTab]->ExrPath = *path;
  m_CaptureMessage = "Exported complete snapshot frame to " + path->string();
}

auto SnapshotAnalysisPanel::BuildWorkspaceDocument(const std::filesystem::path& path) const
    -> Engine::SnapshotPersistence::SnapshotWorkspaceDocument {
  using namespace Engine::SnapshotPersistence;
  SnapshotWorkspaceDocument document;
  document.SelectedTab = m_ActiveTab ? static_cast<uint32_t>(*m_ActiveTab) : 0U;
  document.Tabs.reserve(m_Tabs.size());
  const std::filesystem::path directory = path.parent_path();
  for (const auto& tab : m_Tabs) {
    SnapshotFrameViewState state;
    if (!tab->ExrPath.empty()) {
      std::error_code error;
      state.ExrPath = std::filesystem::relative(tab->ExrPath, directory, error);
      if (error || state.ExrPath.empty()) {
        state.ExrPath = tab->ExrPath;
      }
    }
    state.SelectedPlane = tab->SelectedPlane;
    if (tab->PinnedPixel) {
      state.PinnedPixel = SnapshotPinnedPixelState{tab->PinnedPixel->at(0), tab->PinnedPixel->at(1)};
    }
    if (tab->Roi) {
      state.Roi = SnapshotRoiState{tab->Roi->at(0), tab->Roi->at(1), tab->Roi->at(2) - tab->Roi->at(0),
                                   tab->Roi->at(3) - tab->Roi->at(1)};
    }
    state.Zoom = tab->Zoom;
    state.PanX = tab->Pan[0];
    state.PanY = tab->Pan[1];
    document.Tabs.push_back(std::move(state));
  }
  if (m_ComparisonReference && m_ActiveTab && *m_ComparisonReference < m_Tabs.size() && *m_ActiveTab < m_Tabs.size() &&
      *m_ComparisonReference != *m_ActiveTab) {
    SnapshotComparisonMetric metric = SnapshotComparisonMetric::RgbRms;
    if (m_ComparisonMetric == Engine::SnapshotAnalysis::DifferenceMetric::MeanAbsolute) {
      metric = SnapshotComparisonMetric::AbsoluteError;
    } else if (m_ComparisonMetric == Engine::SnapshotAnalysis::DifferenceMetric::RelativeLuminance) {
      metric = SnapshotComparisonMetric::RelativeLuminance;
    }
    document.Comparison = SnapshotComparisonState{static_cast<uint32_t>(*m_ComparisonReference),
                                                  static_cast<uint32_t>(*m_ActiveTab), metric, m_WipePosition};
  }
  return document;
}

auto SnapshotAnalysisPanel::SaveWorkspaceAs() -> bool {
  if (!m_WorkspaceServices.ChoosePath || !m_WorkspaceServices.WriteExr || !m_WorkspaceServices.SaveWorkspace) {
    m_CaptureMessage = "Workspace save is unavailable.";
    return false;
  }
  const auto path = m_WorkspaceServices.ChoosePath(false, true, m_WorkspacePath);
  if (!path) {
    return false;
  }
  std::vector<std::filesystem::path> framePaths;
  framePaths.reserve(m_Tabs.size());
  for (size_t index = 0; index < m_Tabs.size(); ++index) {
    std::filesystem::path framePath = m_Tabs[index]->ExrPath;
    std::error_code existsError;
    if (framePath.empty() || !std::filesystem::exists(framePath, existsError) || existsError) {
      const std::string stem = path->stem().string() + "-frame-" + std::to_string(index + 1U);
      framePath = path->parent_path() / (stem + ".exr");
      for (uint32_t suffix = 2; std::filesystem::exists(framePath, existsError) && !existsError; ++suffix) {
        framePath = path->parent_path() / (stem + "-" + std::to_string(suffix) + ".exr");
      }
      const auto write = m_WorkspaceServices.WriteExr(m_Tabs[index]->Frame, framePath);
      if (!write) {
        m_CaptureMessage = "Workspace EXR payload write failed: " + write.error().Message;
        return false;
      }
    }
    framePaths.push_back(std::move(framePath));
  }
  auto document = BuildWorkspaceDocument(*path);
  for (size_t index = 0; index < framePaths.size(); ++index) {
    std::error_code error;
    document.Tabs[index].ExrPath = std::filesystem::relative(framePaths[index], path->parent_path(), error);
    if (error || document.Tabs[index].ExrPath.empty()) {
      document.Tabs[index].ExrPath = framePaths[index];
    }
  }
  const auto saved = m_WorkspaceServices.SaveWorkspace(*path, document);
  if (!saved) {
    m_CaptureMessage = "Workspace save failed: " + saved.error().Message;
    return false;
  }
  for (size_t index = 0; index < framePaths.size(); ++index) {
    m_Tabs[index]->ExrPath = framePaths[index];
  }
  m_WorkspacePath = *path;
  m_CaptureMessage = "Saved workspace and referenced snapshot EXR payloads to " + path->string();
  return true;
}

auto SnapshotAnalysisPanel::BuildDisplayPixels(const Tab& tab) const -> std::vector<uint8_t> {
  if (!tab.Frame.HasPlane(tab.SelectedPlane)) {
    return {};
  }
  if (m_ShowHeatmap) {
    std::vector<uint8_t> heatmap = BuildHeatmapPixels(tab);
    if (!heatmap.empty()) {
      return heatmap;
    }
  }
  if (m_ShowWipe) {
    std::vector<uint8_t> wipe = BuildWipePixels(tab);
    if (!wipe.empty()) {
      return wipe;
    }
  }
  const Engine::SnapshotFrame& frame = tab.Frame;
  const size_t pixelCount = static_cast<size_t>(frame.GetWidth()) * frame.GetHeight();
  if (pixelCount == 0 || pixelCount > (std::numeric_limits<size_t>::max() / 4U)) {
    return {};
  }
  std::vector<uint8_t> rgba(pixelCount * 4U, 255U);
  const auto ids = frame.GetMaterialInstanceIds();
  const std::optional<uint32_t> isolatedId =
      m_IsolateMaterial && tab.SelectedPlane == Engine::SnapshotPlane::FinalColor &&
              frame.HasPlane(Engine::SnapshotPlane::MaterialInstanceId) && tab.PinnedPixel &&
              tab.PinnedPixel->at(0) < frame.GetWidth() && tab.PinnedPixel->at(1) < frame.GetHeight()
          ? std::optional<uint32_t>(ids[tab.PinnedPixel->at(1) * frame.GetWidth() + tab.PinnedPixel->at(0)])
          : std::nullopt;
  for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
    auto displayRgb = GetDisplayRgb(frame, tab.SelectedPlane, pixel);
    if (!displayRgb) {
      return {};
    }
    std::array<float, 3> rgb = *displayRgb;
    if (isolatedId && ids[pixel] != *isolatedId) {
      rgb = {0.035F, 0.035F, 0.035F};
    }
    rgba[pixel * 4U] = Encode(rgb[0]);
    rgba[pixel * 4U + 1U] = Encode(rgb[1]);
    rgba[pixel * 4U + 2U] = Encode(rgb[2]);
  }
  return rgba;
}

auto SnapshotAnalysisPanel::BuildWipePixels(const Tab& tab) const -> std::vector<uint8_t> {
  if (!m_ComparisonReference || *m_ComparisonReference >= m_Tabs.size() || !m_ActiveTab ||
      *m_ComparisonReference == *m_ActiveTab) {
    return {};
  }
  const Engine::SnapshotFrame& reference = m_Tabs[*m_ComparisonReference]->Frame;
  if (reference.GetWidth() != tab.Frame.GetWidth() || reference.GetHeight() != tab.Frame.GetHeight() ||
      !reference.HasPlane(tab.SelectedPlane) || !tab.Frame.HasPlane(tab.SelectedPlane)) {
    return {};
  }
  const uint32_t width = tab.Frame.GetWidth();
  const size_t pixelCount = static_cast<size_t>(width) * tab.Frame.GetHeight();
  const uint32_t divider = static_cast<uint32_t>(static_cast<float>(width) * m_WipePosition);
  std::vector<uint8_t> rgba(pixelCount * 4U, 255U);
  for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
    const uint32_t x = static_cast<uint32_t>(pixel % width);
    auto displayRgb = GetDisplayRgb(x < divider ? reference : tab.Frame, tab.SelectedPlane, pixel);
    if (!displayRgb) {
      return {};
    }
    std::array<float, 3> rgb = *displayRgb;
    if (x == divider) {
      rgb = {4.0F, 4.0F, 4.0F};
    }
    rgba[pixel * 4U] = Encode(rgb[0]);
    rgba[pixel * 4U + 1U] = Encode(rgb[1]);
    rgba[pixel * 4U + 2U] = Encode(rgb[2]);
  }
  return rgba;
}

auto SnapshotAnalysisPanel::BuildHeatmapPixels(const Tab& tab) const -> std::vector<uint8_t> {
  if (!m_ComparisonReference || *m_ComparisonReference >= m_Tabs.size() || !m_ActiveTab ||
      *m_ComparisonReference == *m_ActiveTab) {
    return {};
  }
  const Engine::SnapshotFrame& reference = m_Tabs[*m_ComparisonReference]->Frame;
  if (!reference.HasPlane(tab.SelectedPlane) || !tab.Frame.HasPlane(tab.SelectedPlane)) {
    return {};
  }
  auto result = Engine::SnapshotAnalysis::ComputeComparison(
      ToAnalysisView(reference, tab.SelectedPlane), ToAnalysisView(tab.Frame, tab.SelectedPlane), m_ComparisonMetric);
  if (!result) {
    return {};
  }
  const auto normalized =
      Engine::SnapshotAnalysis::MakeHeatmapDisplayValues(*result, m_HeatmapMinimum, m_HeatmapMaximum);
  if (!normalized) {
    return {};
  }
  std::vector<uint8_t> rgba(static_cast<size_t>(tab.Frame.GetWidth()) * tab.Frame.GetHeight() * 4U, 255U);
  for (size_t pixel = 0; pixel < normalized->size(); ++pixel) {
    const float value = std::isfinite((*normalized)[pixel]) ? (*normalized)[pixel] : 0.0F;
    rgba[pixel * 4U] = static_cast<uint8_t>(std::lround(value * 255.0F));
    rgba[pixel * 4U + 1U] = static_cast<uint8_t>(std::lround((1.0F - value) * 96.0F));
    rgba[pixel * 4U + 2U] = static_cast<uint8_t>(std::lround((1.0F - value) * 255.0F));
  }
  return rgba;
}

void SnapshotAnalysisPanel::DrawFrame(Tab& tab, size_t tabIndex) {
  const Engine::SnapshotFrame& frame = tab.Frame;
  const auto dimensions = std::to_string(frame.GetWidth()) + " × " + std::to_string(frame.GetHeight());
  ImGui::Text("%s  |  %s", tab.Name.c_str(), dimensions.c_str());
  const std::vector<Engine::SnapshotPlane> availablePlanes = GetAvailableSnapshotPlanes(frame);
  std::vector<const char*> availablePlaneNames;
  availablePlaneNames.reserve(availablePlanes.size());
  int selectedPlaneIndex = 0;
  for (size_t index = 0; index < availablePlanes.size(); ++index) {
    availablePlaneNames.push_back(GetSnapshotPlaneName(availablePlanes[index]));
    if (availablePlanes[index] == tab.SelectedPlane) {
      selectedPlaneIndex = static_cast<int>(index);
    }
  }
  if (availablePlaneNames.empty()) {
    ImGui::TextDisabled("Frame has no available analysis planes.");
    return;
  }
  const ImVec2 workspaceSize = ImGui::GetContentRegionAvail();
  const float toolsWidth = (std::min)(480.0F, (std::max)(240.0F, workspaceSize.x * 0.30F));
  if (!ImGui::BeginTable("snapshot_workspace_layout", 2,
                         ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings,
                         workspaceSize)) {
    return;
  }
  ImGui::TableSetupColumn("Analysis tools", ImGuiTableColumnFlags_WidthFixed, toolsWidth);
  ImGui::TableSetupColumn("Captured frame", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableNextRow(ImGuiTableRowFlags_None, workspaceSize.y);
  ImGui::TableSetColumnIndex(0);
  ImGui::BeginChild("snapshot_analysis_tools", ImVec2(0.0F, workspaceSize.y), true);
  ImGui::TextUnformatted("Analysis tools");
  ImGui::Separator();
  if (ImGui::Button("Back to editor") && m_ExitFocusedHandler) {
    m_ExitFocusedHandler();
    if (m_ClearImage) {
      m_ClearImage();
    }
  }
  ImGui::Separator();
  if (ImGui::Combo("Plane", &selectedPlaneIndex, availablePlaneNames.data(),
                   static_cast<int>(availablePlaneNames.size()))) {
    tab.SelectedPlane = availablePlanes[static_cast<size_t>(selectedPlaneIndex)];
    InvalidateDisplay();
  }
  if (tab.SelectedPlane == Engine::SnapshotPlane::FinalColor) {
    if (frame.HasPlane(Engine::SnapshotPlane::MaterialInstanceId)) {
      ImGui::SameLine();
      if (ImGui::Checkbox("Isolate pinned material", &m_IsolateMaterial)) {
        InvalidateDisplay();
      }
    } else {
      ImGui::SameLine();
      ImGui::TextDisabled("Material isolation unavailable: material IDs were not captured.");
    }
  }
  if (m_Tabs.size() > 1U) {
    ImGui::SeparatorText("Frame comparison");
    std::vector<const char*> frameNames;
    frameNames.reserve(m_Tabs.size() + 1U);
    frameNames.push_back("Off");
    for (const auto& item : m_Tabs) {
      frameNames.push_back(item->Name.c_str());
    }
    const int reference = m_ComparisonReference && *m_ComparisonReference < m_Tabs.size()
                              ? static_cast<int>(*m_ComparisonReference + 1U)
                              : 0;
    int selection = reference;
    if (ImGui::Combo("Reference frame", &selection, frameNames.data(), static_cast<int>(frameNames.size()))) {
      m_ComparisonReference = selection == 0 ? std::nullopt : std::optional<size_t>(static_cast<size_t>(selection - 1));
      m_DisplayDirty = true;
    }
    if (m_ComparisonReference && *m_ComparisonReference < m_Tabs.size() && *m_ComparisonReference != tabIndex) {
      int metric = static_cast<int>(m_ComparisonMetric);
      constexpr const char* metricNames[] = {"Mean absolute error", "RGB RMS", "Relative luminance"};
      if (ImGui::Combo("Metric", &metric, metricNames, 3)) {
        m_ComparisonMetric = static_cast<Engine::SnapshotAnalysis::DifferenceMetric>(metric);
        m_DisplayDirty = true;
      }
      const auto& referenceFrame = m_Tabs[*m_ComparisonReference]->Frame;
      const bool equalDimensions =
          referenceFrame.GetWidth() == frame.GetWidth() && referenceFrame.GetHeight() == frame.GetHeight();
      const bool selectedPlaneAvailable =
          referenceFrame.HasPlane(tab.SelectedPlane) && frame.HasPlane(tab.SelectedPlane);
      const auto comparison =
          Engine::SnapshotAnalysis::ComputeComparison(ToAnalysisView(referenceFrame, tab.SelectedPlane),
                                                      ToAnalysisView(frame, tab.SelectedPlane), m_ComparisonMetric);
      const bool heatmapAvailable = equalDimensions && selectedPlaneAvailable && comparison.has_value();
      const bool wipeAvailable = equalDimensions && selectedPlaneAvailable;
      if (!heatmapAvailable && m_ShowHeatmap) {
        m_ShowHeatmap = false;
        m_DisplayDirty = true;
      }
      if (!wipeAvailable && m_ShowWipe) {
        m_ShowWipe = false;
        m_DisplayDirty = true;
      }
      ImGui::BeginDisabled(!heatmapAvailable);
      if (ImGui::Checkbox("Display heatmap", &m_ShowHeatmap)) {
        m_DisplayDirty = true;
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(!wipeAvailable);
      if (ImGui::Checkbox("Wipe A/B", &m_ShowWipe)) {
        m_DisplayDirty = true;
      }
      ImGui::EndDisabled();
      if (m_ShowWipe && ImGui::SliderFloat("Wipe position", &m_WipePosition, 0.0F, 1.0F)) {
        m_DisplayDirty = true;
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(90.0F);
      if (ImGui::DragFloat("Min", &m_HeatmapMinimum, 0.01F, 0.0F, 100000.0F)) {
        m_DisplayDirty = true;
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(90.0F);
      if (ImGui::DragFloat("Max", &m_HeatmapMaximum, 0.01F, 0.0001F, 100000.0F)) {
        m_DisplayDirty = true;
      }
      if (m_ShowHeatmap) {
        const char* metricLabel = metricNames[static_cast<size_t>(m_ComparisonMetric)];
        ImGui::Text("Heatmap legend: %s | %.6g (blue) to %.6g (red)", metricLabel, m_HeatmapMinimum, m_HeatmapMaximum);
        if (m_HeatmapMaximum <= m_HeatmapMinimum) {
          ImGui::TextDisabled("Heatmap unavailable: maximum must be greater than minimum.");
        }
      }
      if (!selectedPlaneAvailable) {
        ImGui::TextDisabled("Comparison unavailable: the selected plane is missing from one of the frames.");
      } else if (!equalDimensions) {
        ImGui::TextDisabled("Comparison unavailable: frames have different dimensions.");
      } else if (comparison) {
        ImGui::Text("B - A | valid %llu | invalid %llu | mean %.8g | RMS %.8g | max %.8g",
                    static_cast<unsigned long long>(comparison->Statistics.ValidSampleCount),
                    static_cast<unsigned long long>(comparison->Statistics.InvalidSampleCount),
                    comparison->Statistics.Mean, comparison->Statistics.Rms, comparison->Statistics.Maximum);
        if (tab.PinnedPixel) {
          const size_t pixel = static_cast<size_t>(tab.PinnedPixel->at(1)) * frame.GetWidth() + tab.PinnedPixel->at(0);
          if (pixel < comparison->ScalarValues.size() && comparison->ValidSamples[pixel] != 0U) {
            ImGui::Text("Pinned raw error: %.10g", comparison->ScalarValues[pixel]);
          }
        }
      } else {
        ImGui::TextDisabled("Frames cannot be compared: %s", comparison.error().Message.c_str());
      }
      if (!wipeAvailable) {
        ImGui::TextDisabled("Wipe unavailable: frames need equal dimensions and the selected plane.");
      }
      if (!heatmapAvailable && equalDimensions && selectedPlaneAvailable) {
        ImGui::TextDisabled("Heatmap unavailable for this plane and metric: %s", comparison.error().Message.c_str());
      }
    }
  }

  if (tab.PinnedPixel) {
    const uint32_t x = tab.PinnedPixel->at(0);
    const uint32_t y = tab.PinnedPixel->at(1);
    const size_t pixel = static_cast<size_t>(y) * frame.GetWidth() + x;
    ImGui::Text("Pinned pixel (%u, %u)", x, y);
    if (frame.HasPlane(Engine::SnapshotPlane::MaterialInstanceId)) {
      ImGui::SameLine();
      ImGui::Text("material id %u", frame.GetMaterialInstanceIds()[pixel]);
    } else {
      ImGui::SameLine();
      ImGui::TextDisabled("material ID plane unavailable");
    }
    if (frame.HasPlane(Engine::SnapshotPlane::Overdraw)) {
      ImGui::SameLine();
      ImGui::Text("overdraw %u", frame.GetOverdraw()[pixel]);
    } else {
      ImGui::SameLine();
      ImGui::TextDisabled("overdraw plane unavailable");
    }
    if (ImGui::TreeNode("Captured AOV values")) {
      for (const auto& plane : frame.GetFloatPlanes()) {
        const size_t base = pixel * plane.ChannelCount;
        ImGui::Text("%s:", Engine::RenderOutput::GetDescriptor(plane.Aov).Name);
        ImGui::SameLine();
        for (uint32_t channel = 0; channel < plane.ChannelCount; ++channel) {
          ImGui::Text("%.8g%s", plane.Pixels[base + channel], channel + 1U == plane.ChannelCount ? "" : ", ");
          if (channel + 1U != plane.ChannelCount) {
            ImGui::SameLine();
          }
        }
      }
      ImGui::TreePop();
    }
    if (!frame.HasPlane(Engine::SnapshotPlane::MaterialInstanceId)) {
      ImGui::TextDisabled("Captured material data unavailable: material IDs were not captured.");
    } else if (ImGui::TreeNode("Captured material")) {
      const uint32_t materialId = frame.GetMaterialInstanceIds()[pixel];
      bool found = false;
      for (const auto& material : frame.GetManifest()) {
        if (material.Id == materialId) {
          found = true;
          ImGui::Text("Entity: %s", material.EntityIdentity.c_str());
          ImGui::Text("Material: %s", material.MaterialIdentity.c_str());
          ImGui::Text("Slot: %u  Submesh: %u", material.MaterialSlot, material.SubmeshIndex);
          ImGui::Text("Base color: %.4f, %.4f, %.4f, %.4f", material.CapturedValues.baseColor[0],
                      material.CapturedValues.baseColor[1], material.CapturedValues.baseColor[2],
                      material.CapturedValues.baseColor[3]);
          ImGui::Text("Emissive: %.4f, %.4f, %.4f", material.CapturedValues.emissive[0],
                      material.CapturedValues.emissive[1], material.CapturedValues.emissive[2]);
          ImGui::Text("Metallic %.4f | Roughness %.4f | GTR gamma %.4f | IOR %.4f", material.CapturedValues.metallic,
                      material.CapturedValues.roughness, material.CapturedValues.gtrGamma, material.CapturedValues.ior);
          ImGui::Text("Specular F0: %.4f, %.4f, %.4f", material.CapturedValues.specularF0[0],
                      material.CapturedValues.specularF0[1], material.CapturedValues.specularF0[2]);
          ImGui::Text("BRDF: %s | workflow: %s | specular: %s", material.CapturedValues.brdfModel.c_str(),
                      material.CapturedValues.workflow.c_str(), material.CapturedValues.specularMode.c_str());
          ImGui::Text("Alpha: %s | cutoff %.4f | double-sided: %s", material.CapturedValues.alphaMode.c_str(),
                      material.CapturedValues.alphaCutoff, material.CapturedValues.doubleSided ? "yes" : "no");
          ImGui::Text("Base color texture: %s", material.CapturedValues.baseColorTextureGuid
                                                    ? material.CapturedValues.baseColorTextureGuid->c_str()
                                                    : "none");
          ImGui::Text("Normal texture: %s", material.CapturedValues.normalTextureGuid
                                                ? material.CapturedValues.normalTextureGuid->c_str()
                                                : "none");
          if (!material.ResourceIdentities.empty()) {
            ImGui::Text("Captured resources:");
            for (const std::string& resource : material.ResourceIdentities) {
              ImGui::BulletText("%s", resource.c_str());
            }
          }
          break;
        }
      }
      if (!found) {
        ImGui::TextDisabled("Background pixel has no captured material.");
      }
      ImGui::TreePop();
    }
  }

  if (ImGui::TreeNode("Capture metadata")) {
    const auto& metadata = frame.GetMetadata();
    ImGui::Text("Captured: %s", metadata.CaptureTimeUtc.c_str());
    ImGui::Text("Scene: %s", metadata.SceneIdentity.c_str());
    ImGui::Text("Camera: %s", metadata.Camera.Identity.c_str());
    ImGui::Text("Camera output: %u × %u | captured: %u × %u", metadata.Camera.AuthoredWidth,
                metadata.Camera.AuthoredHeight, metadata.OutputWidth, metadata.OutputHeight);
    ImGui::Text("Camera position: %.5g, %.5g, %.5g", metadata.Camera.Position[0], metadata.Camera.Position[1],
                metadata.Camera.Position[2]);
    ImGui::Text("Vertical FOV %.5g° | near %.5g | far %.5g", metadata.Camera.FieldOfViewDegrees,
                metadata.Camera.NearPlane, metadata.Camera.FarPlane);
    ImGui::Text("Shadows: %s | map %u | max casters %u | samples %u",
                metadata.RenderSettings.ShadowsEnabled ? "on" : "off", metadata.RenderSettings.ShadowMapResolution,
                metadata.RenderSettings.MaxShadowCasters, metadata.RenderSettings.SampleCount);
    ImGui::Text("Color: Rec. 709 linear | luminance: %s",
                metadata.Color.LuminanceUnit == Engine::SnapshotLuminanceUnit::Nits ? "nits" : "scene-linear relative");
    if (metadata.Color.NitsPerSceneUnit) {
      ImGui::Text("Nits per scene-linear unit: %.8g", *metadata.Color.NitsPerSceneUnit);
    }
    ImGui::TreePop();
  }

  const auto* colorPlane = FindPlane(frame, Engine::RenderAov::FinalColor);
  if (tab.Roi && colorPlane != nullptr) {
    ImGui::SeparatorText("ROI histogram");
    ImGui::SetNextItemWidth(110.0F);
    (void)ImGui::DragFloat("Histogram min", &tab.HistogramMinimum, 0.05F, -100000.0F, 100000.0F, "%.4g");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0F);
    (void)ImGui::DragFloat("Histogram max", &tab.HistogramMaximum, 0.05F, -100000.0F, 100000.0F, "%.4g");
    const Engine::SnapshotAnalysis::PlaneView view = ToAnalysisView(frame, Engine::SnapshotPlane::FinalColor);
    Engine::SnapshotAnalysis::RoiHistogramOptions options;
    options.Region = {tab.Roi->at(0), tab.Roi->at(1), tab.Roi->at(2), tab.Roi->at(3)};
    options.BinCount = 64;
    options.Minimum = tab.HistogramMinimum;
    options.Maximum = tab.HistogramMaximum;
    const auto histogram = Engine::SnapshotAnalysis::ComputeRoiHistogram(view, options);
    if (histogram) {
      ImGui::Text("ROI [%u,%u) × [%u,%u) | valid %llu | invalid %llu | out of range %llu | luminance %s",
                  tab.Roi->at(0), tab.Roi->at(2), tab.Roi->at(1), tab.Roi->at(3),
                  static_cast<unsigned long long>(histogram->ValidPixelCount),
                  static_cast<unsigned long long>(histogram->InvalidSampleCount),
                  static_cast<unsigned long long>(histogram->OutOfRangeValueCount),
                  histogram->LuminanceUnits == Engine::SnapshotAnalysis::LuminanceUnit::Nits ? "nits"
                                                                                             : "scene-linear relative");
      std::array<std::vector<float>, 4> histogramFloats;
      for (size_t channel = 0; channel < histogramFloats.size(); ++channel) {
        histogramFloats[channel].reserve(histogram->Bins[channel].size());
        for (uint64_t count : histogram->Bins[channel]) {
          histogramFloats[channel].push_back(static_cast<float>(count));
        }
      }
      ImGui::PlotHistogram("R", histogramFloats[0].data(), static_cast<int>(histogramFloats[0].size()), 0, nullptr,
                           FLT_MAX, FLT_MAX, ImVec2(0.0F, 45.0F));
      ImGui::PlotHistogram("G", histogramFloats[1].data(), static_cast<int>(histogramFloats[1].size()), 0, nullptr,
                           FLT_MAX, FLT_MAX, ImVec2(0.0F, 45.0F));
      ImGui::PlotHistogram("B", histogramFloats[2].data(), static_cast<int>(histogramFloats[2].size()), 0, nullptr,
                           FLT_MAX, FLT_MAX, ImVec2(0.0F, 45.0F));
      ImGui::PlotHistogram("Y", histogramFloats[3].data(), static_cast<int>(histogramFloats[3].size()), 0, nullptr,
                           FLT_MAX, FLT_MAX, ImVec2(0.0F, 45.0F));
    } else {
      ImGui::TextDisabled("ROI analysis unavailable: %s", histogram.error().Message.c_str());
    }
  } else if (tab.Roi) {
    ImGui::TextDisabled("ROI histogram unavailable: final_color was not captured.");
  }
  ImGui::EndChild();

  ImGui::TableSetColumnIndex(1);
  ImGui::BeginChild("snapshot_canvas", ImVec2(0.0F, workspaceSize.y), true,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float fit =
      CameraPreviewImageLayout::ComputeFitScale(frame.GetWidth(), frame.GetHeight(), available.x, available.y);
  const float displayScale = (std::max)(fit, 0.001F) * tab.Zoom;
  const ImVec2 size(static_cast<float>(frame.GetWidth()) * displayScale,
                    static_cast<float>(frame.GetHeight()) * displayScale);
  tab.Pan[0] = CameraPreviewImageLayout::ClampPanOffset(tab.Pan[0], (std::max)(size.x - available.x, 0.0F));
  tab.Pan[1] = CameraPreviewImageLayout::ClampPanOffset(tab.Pan[1], (std::max)(size.y - available.y, 0.0F));
  const ImVec2 origin(ImGui::GetCursorScreenPos().x + tab.Pan[0], ImGui::GetCursorScreenPos().y + tab.Pan[1]);
  bool displayAvailable = true;
  if (m_DisplayDirty || m_UploadedTab != tabIndex || m_UploadedPlane != tab.SelectedPlane) {
    const std::vector<uint8_t> pixels = BuildDisplayPixels(tab);
    if (pixels.empty()) {
      displayAvailable = false;
      if (m_ClearImage) {
        m_ClearImage();
      }
      m_UploadedTab = static_cast<size_t>(-1);
      m_DisplayDirty = false;
    } else if (m_UploadImage && m_UploadImage(frame.GetWidth(), frame.GetHeight(), pixels)) {
      m_UploadedTab = tabIndex;
      m_UploadedPlane = tab.SelectedPlane;
      m_DisplayDirty = false;
    }
  }
  if (displayAvailable && m_GetTexture) {
    void* texture = m_GetTexture();
    if (texture != nullptr) {
      ImGui::SetCursorScreenPos(origin);
      ImGui::Image(texture, size);
      const ImVec2 imageMin = ImGui::GetItemRectMin();
      const ImVec2 imageMax = ImGui::GetItemRectMax();
      const bool imageHovered = ImGui::IsItemHovered();
      if (imageHovered) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0F) {
          tab.Zoom = CameraPreviewImageLayout::ClampZoom(tab.Zoom * std::pow(1.12F, io.MouseWheel));
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyShift) {
          const uint32_t x = static_cast<uint32_t>(
              std::clamp((io.MousePos.x - imageMin.x) / displayScale, 0.0F, static_cast<float>(frame.GetWidth() - 1U)));
          const uint32_t y = static_cast<uint32_t>(std::clamp((io.MousePos.y - imageMin.y) / displayScale, 0.0F,
                                                              static_cast<float>(frame.GetHeight() - 1U)));
          tab.PinnedPixel = std::array<uint32_t, 2>{x, y};
          InvalidateDisplay();
        }
        if (io.KeyShift && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          m_RoiStart = std::array<uint32_t, 2>{
              static_cast<uint32_t>(std::clamp((io.MousePos.x - imageMin.x) / displayScale, 0.0F,
                                               static_cast<float>(frame.GetWidth() - 1U))),
              static_cast<uint32_t>(std::clamp((io.MousePos.y - imageMin.y) / displayScale, 0.0F,
                                               static_cast<float>(frame.GetHeight() - 1U)))};
        }
        if (m_RoiStart && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
          const uint32_t x = static_cast<uint32_t>(
              std::clamp((io.MousePos.x - imageMin.x) / displayScale, 0.0F, static_cast<float>(frame.GetWidth() - 1U)));
          const uint32_t y = static_cast<uint32_t>(std::clamp((io.MousePos.y - imageMin.y) / displayScale, 0.0F,
                                                              static_cast<float>(frame.GetHeight() - 1U)));
          tab.Roi =
              std::array<uint32_t, 4>{(std::min)(m_RoiStart->at(0), x), (std::min)(m_RoiStart->at(1), y),
                                      (std::max)(m_RoiStart->at(0), x) + 1U, (std::max)(m_RoiStart->at(1), y) + 1U};
          m_RoiStart.reset();
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
          const ImVec2 delta = ImGui::GetIO().MouseDelta;
          tab.Pan[0] += delta.x;
          tab.Pan[1] += delta.y;
        }
        ImGui::SetCursorScreenPos(imageMax);
      } else {
        ImGui::Dummy(size);
      }
    } else {
      ImGui::TextDisabled("Snapshot image texture is unavailable.");
    }
  } else if (!displayAvailable) {
    ImGui::TextDisabled("Selected snapshot plane is unavailable in this frame.");
  }
  ImGui::TextDisabled("Wheel zooms; middle drag pans. Click pins a source pixel; Shift-drag sets a source-space ROI.");
  ImGui::EndChild();
  ImGui::EndTable();
}

void SnapshotAnalysisPanel::Draw() {
  RefreshCameraOptions();
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::SetNextWindowViewport(viewport->ID);
  if (m_FocusRequested) {
    ImGui::SetNextWindowFocus();
    m_FocusRequested = false;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
  constexpr ImGuiWindowFlags focusedWorkspaceFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
  const bool contentsVisible = ImGui::Begin("Snapshot Analysis##FocusedWorkspace", nullptr, focusedWorkspaceFlags);
  if (contentsVisible) {
    if (ImGui::Button("Exit focused view")) {
      if (m_ExitFocusedHandler) {
        m_ExitFocusedHandler();
      }
      if (m_ClearImage) {
        m_ClearImage();
      }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Captured frames remain stored in the workspace while you return to the editor.");
    ImGui::Separator();
    if (m_CameraOptions.empty()) {
      ImGui::TextDisabled("No valid authored camera is available in the active scene.");
    } else {
      std::vector<const char*> cameraNames;
      cameraNames.reserve(m_CameraOptions.size());
      int selectedCameraIndex = 0;
      for (size_t index = 0; index < m_CameraOptions.size(); ++index) {
        cameraNames.push_back(m_CameraOptions[index].Label.c_str());
        if (m_SelectedCamera == m_CameraOptions[index].Entity) {
          selectedCameraIndex = static_cast<int>(index);
        }
      }
      if (ImGui::Combo("Authored camera", &selectedCameraIndex, cameraNames.data(),
                       static_cast<int>(cameraNames.size()))) {
        m_SelectedCamera = m_CameraOptions[static_cast<size_t>(selectedCameraIndex)].Entity;
      }
    }
    ImGui::Text("Capture planes");
    if (ImGui::BeginTable("snapshot_capture_planes", 4)) {
      for (size_t index = 0; index < SnapshotPlanes.size(); ++index) {
        ImGui::TableNextColumn();
        ImGui::PushID(static_cast<int>(index));
        bool enabled = IsSnapshotCapturePlaneEnabled(m_SelectedCapturePlanes, SnapshotPlanes[index]);
        if (ImGui::Checkbox(SnapshotPlaneNames[index], &enabled)) {
          m_SelectedCapturePlanes =
              SetSnapshotCapturePlaneEnabled(m_SelectedCapturePlanes, SnapshotPlanes[index], enabled);
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (!HasSnapshotCapturePlaneSelection(m_SelectedCapturePlanes)) {
      ImGui::TextDisabled("Select at least one capture plane.");
    }
    ImGui::InputScalar("Width override (0 = authored)", ImGuiDataType_U32, &m_WidthOverride);
    ImGui::InputScalar("Height override (0 = authored)", ImGuiDataType_U32, &m_HeightOverride);
    ImGui::BeginDisabled(m_CaptureTicket.has_value() || !m_SelectedCamera ||
                         !HasSnapshotCapturePlaneSelection(m_SelectedCapturePlanes));
    if (ImGui::Button("Capture selected camera")) {
      StartCapture();
    }
    ImGui::EndDisabled();
    if (m_CaptureTicket) {
      ImGui::SameLine();
      if (ImGui::Button("Cancel capture") && m_CancelCapture) {
        const Engine::Status canceled = m_CancelCapture(*m_CaptureTicket);
        if (canceled) {
          m_CaptureTicket.reset();
          m_CaptureMessage = "Capture canceled; no frame was published.";
        } else {
          m_CaptureMessage = "Capture cancellation failed: " + canceled.error().Message;
        }
      }
      if (m_GetCaptureStatus) {
        const auto status = m_GetCaptureStatus(*m_CaptureTicket);
        if (status) {
          ImGui::SameLine();
          ImGui::Text("%.0f%%", std::clamp(status->Progress, 0.0F, 1.0F) * 100.0F);
        }
      }
    }
    if (ImGui::Button("Open EXR...")) {
      OpenExr();
    }
    ImGui::SameLine();
    if (ImGui::Button("Open .aframe...")) {
      OpenWorkspace();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_ActiveTab.has_value());
    if (ImGui::Button("Export frame EXR...")) {
      SaveCurrentFrame();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_Tabs.empty());
    if (ImGui::Button("Save workspace .aframe...")) {
      (void)SaveWorkspaceAs();
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", m_CaptureMessage.c_str());
    if (m_Tabs.empty()) {
      ImGui::Separator();
      ImGui::TextDisabled("Capture an authored scene camera or open a snapshot file to create a frame tab.");
    } else if (ImGui::BeginTabBar("snapshot_frames")) {
      for (size_t index = 0; index < m_Tabs.size(); ++index) {
        const std::string label = m_Tabs[index]->Name + "###snapshot_" + std::to_string(index);
        if (ImGui::BeginTabItem(label.c_str())) {
          if (m_ActiveTab != index) {
            m_ActiveTab = index;
            m_DisplayDirty = true;
          }
          DrawFrame(*m_Tabs[index], index);
          ImGui::EndTabItem();
        }
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

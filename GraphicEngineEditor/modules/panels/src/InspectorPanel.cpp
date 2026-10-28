#include "panels/include/InspectorPanel.h"
#include "InspectorWidgetIds.h"
#include "events/include/AssetImportRequestedEvent.h"
#include "events/include/MaterialAssetEditRequestedEvent.h"
#include "events/include/MaterialAssetSelectionRequestedEvent.h"
#include "events/include/PrismNdfApplyRequestedEvent.h"
#include "events/include/SceneAmbientLightChangedEvent.h"
#include "panels/include/AssetDragDropPayload.h"
#include "panels/include/AssetDropAssignment.h"
#include "panels/include/TransformRotation.h"
#include "panels/include/TransformScaleLock.h"
#include "rendering/include/renderer/PrismNdfMath.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <imgui.h>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace {
using Mutation = EcsComponentMutationRequestedEvent;
constexpr char kCameraPostProcessEffectPayload[] = "GE_CAMERA_POST_PROCESS_EFFECT";

auto PrismPhaseLabel(Engine::PrismNdfInferencePhase phase) -> const char* {
  switch (phase) {
  case Engine::PrismNdfInferencePhase::Queued:
    return "Queued";
  case Engine::PrismNdfInferencePhase::Tokenizing:
    return "Tokenizing prompt";
  case Engine::PrismNdfInferencePhase::Encoding:
    return "Encoding prompt";
  case Engine::PrismNdfInferencePhase::Retrieving:
    return "Retrieving model features";
  case Engine::PrismNdfInferencePhase::Decoding:
    return "Decoding NDF";
  case Engine::PrismNdfInferencePhase::Fitting:
    return "Fitting Zernike coefficients";
  case Engine::PrismNdfInferencePhase::Completed:
    return "Complete";
  case Engine::PrismNdfInferencePhase::Failed:
    return "Failed";
  case Engine::PrismNdfInferencePhase::Cancelled:
    return "Cancelled";
  }
  return "Working";
}

auto EvaluatePreviewNdf(const Engine::PrismNdfData& data, float x, float y) -> float {
  if (x * x + y * y > 1.0F) {
    return 0.0F;
  }
  double value = 0.0;
  for (uint32_t index = 0; index < Engine::PrismNdfCoefficientCount; ++index) {
    value +=
        static_cast<double>(data.zernikeCoefficients[index]) * Engine::Renderer::EvaluatePrismZernikeBasis(index, x, y);
  }
  const float gridX = (x + 1.0F) * 0.5F * static_cast<float>(Engine::PrismNdfGridSize - 1);
  const float gridY = (y + 1.0F) * 0.5F * static_cast<float>(Engine::PrismNdfGridSize - 1);
  const uint32_t x0 = static_cast<uint32_t>(gridX);
  const uint32_t y0 = static_cast<uint32_t>(gridY);
  const uint32_t x1 = (std::min)(x0 + 1, Engine::PrismNdfGridSize - 1);
  const uint32_t y1 = (std::min)(y0 + 1, Engine::PrismNdfGridSize - 1);
  const float tx = gridX - static_cast<float>(x0);
  const float ty = gridY - static_cast<float>(y0);
  const auto sample = [&data](uint32_t sx, uint32_t sy) -> double {
    return data.residuals[static_cast<size_t>(sy) * Engine::PrismNdfGridSize + sx];
  };
  const double top = std::lerp(sample(x0, y0), sample(x1, y0), static_cast<double>(tx));
  const double bottom = std::lerp(sample(x0, y1), sample(x1, y1), static_cast<double>(tx));
  value = (std::max)(0.0, value + std::lerp(top, bottom, static_cast<double>(ty)));
  return static_cast<float>(value * data.normalization);
}

auto HeatColor(float value, float maximum) -> ImU32 {
  const float t = std::clamp(value / (std::max)(maximum, 1.0e-8F), 0.0F, 1.0F);
  return ImGui::GetColorU32(ImVec4(std::clamp(1.8F * t - 0.7F, 0.0F, 1.0F),
                                   std::clamp(1.7F - std::abs(2.0F * t - 1.0F) * 1.7F, 0.0F, 1.0F),
                                   std::clamp(1.0F - 1.6F * t, 0.0F, 1.0F), 1.0F));
}

auto Matches(const char* name, const char* filter) -> bool {
  if (filter[0] == '\0') {
    return true;
  }
  std::string haystack(name);
  std::string needle(filter);
  std::ranges::transform(haystack, haystack.begin(),
                         [](unsigned char value) -> char { return static_cast<char>(std::tolower(value)); });
  std::ranges::transform(needle, needle.begin(),
                         [](unsigned char value) -> char { return static_cast<char>(std::tolower(value)); });
  return haystack.contains(needle);
}

auto DrawComponentHeader(const char* label, const char* componentName, const char* tableId,
                         Mutation::Component component, const std::function<void(Mutation::Component)>& emitRemove)
    -> bool {
  const float buttonSize = ImGui::GetFrameHeight();
  const bool tableVisible =
      ImGui::BeginTable(tableId, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings);
  if (!tableVisible) {
    return false;
  }

  ImGui::TableSetupColumn("Component header", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("Remove component", ImGuiTableColumnFlags_WidthFixed, buttonSize);
  ImGui::TableNextRow(ImGuiTableRowFlags_None, buttonSize);
  ImGui::TableSetColumnIndex(0);
  const bool open = ImGui::CollapsingHeader(label);
  ImGui::TableSetColumnIndex(1);
  // The button has its own table cell, so clicking it cannot activate the header beside it.
  const std::string buttonId = std::string("##Remove ") + componentName + " component";
  if (ImGui::Button(buttonId.c_str(), ImVec2(buttonSize, buttonSize))) {
    emitRemove(component);
  }
  if (ImGui::IsItemHovered()) {
    const std::string tooltip = std::string("Remove ") + componentName + " component";
    ImGui::SetTooltip("%s", tooltip.c_str());
  }

  const ImVec2 buttonMin = ImGui::GetItemRectMin();
  const ImVec2 buttonMax = ImGui::GetItemRectMax();
  const float centerX = (buttonMin.x + buttonMax.x) * 0.5F;
  const float centerY = (buttonMin.y + buttonMax.y) * 0.5F;
  const ImU32 iconColor = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const float scale = buttonSize / 20.0F;
  drawList->AddRectFilled(ImVec2(centerX - 4.0F * scale, centerY - 2.0F * scale),
                          ImVec2(centerX + 4.0F * scale, centerY + 5.0F * scale), iconColor, 1.0F * scale);
  drawList->AddRectFilled(ImVec2(centerX - 5.0F * scale, centerY - 4.0F * scale),
                          ImVec2(centerX + 5.0F * scale, centerY - 3.0F * scale), iconColor, 0.5F * scale);
  drawList->AddRectFilled(ImVec2(centerX - 2.0F * scale, centerY - 6.0F * scale),
                          ImVec2(centerX + 2.0F * scale, centerY - 5.0F * scale), iconColor, 0.5F * scale);
  ImGui::EndTable();
  return open;
}
} // namespace

InspectorPanel::InspectorPanel(PrismNdfGenerationAdapter generation) : m_PrismGeneration(std::move(generation)) {}

InspectorPanel::~InspectorPanel() { ResetPrismNdfEditor(true); }

void InspectorPanel::ResetPrismNdfEditor(bool cancelRequest) {
  if (cancelRequest && m_PrismRequestId && m_PrismGeneration.cancel) {
    (void)m_PrismGeneration.cancel(*m_PrismRequestId);
  }
  m_PrismRequestEligible = false;
  m_PrismEditorRequested = false;
  m_PrismPreview.reset();
  m_PrismPreviewFromSaved = false;
  m_PrismPreviewDirty = false;
  m_PrismStatus.reset();
  m_PrismPrompt.fill('\0');
  m_PrismDirectional = false;
}

void InspectorPanel::PollPrismGeneration() {
  if (!m_PrismRequestId || !m_PrismGeneration.poll) {
    return;
  }
  const auto status = m_PrismGeneration.poll(*m_PrismRequestId);
  if (!status) {
    return;
  }
  m_PrismStatus = *status;
  const bool terminal = status->phase == Engine::PrismNdfInferencePhase::Completed ||
                        status->phase == Engine::PrismNdfInferencePhase::Failed ||
                        status->phase == Engine::PrismNdfInferencePhase::Cancelled;
  if (!terminal) {
    return;
  }
  const auto* currentMesh = m_Scene != nullptr && m_Scene->World().IsAlive(m_PrismRequestEntity)
                                ? m_Scene->World().Get<Engine::Ecs::MeshRenderer>(m_PrismRequestEntity)
                                : nullptr;
  const bool requestTargetStillSelected = currentMesh != nullptr && m_SelectedEntity == m_PrismRequestEntity &&
                                          currentMesh->MaterialGuid == m_PrismRequestMaterialGuid;
  if (status->phase == Engine::PrismNdfInferencePhase::Completed && m_PrismRequestEligible &&
      requestTargetStillSelected && status->result && m_PrismRequestEntity == m_PrismEditorEntity &&
      m_PrismRequestMaterialGuid == m_PrismMaterialGuid) {
    m_PrismPreview = status->result;
    m_PrismPreviewFromSaved = false;
    m_PrismPreviewDirty = true;
    m_PrismPreviewPrompt = m_PrismRequestPrompt;
    m_PrismPreviewDirectional = m_PrismRequestDirectional;
  }
  m_PrismRequestId.reset();
  m_PrismRequestEligible = false;
}

void InspectorPanel::OnEvent(Engine::Events::Event& event) {
  Engine::Events::EventDispatcher dispatcher(event);
  dispatcher.Dispatch<EcsEntitySelectedEvent>([this](const auto& selection) -> bool {
    if (m_SelectedEntity != selection.GetEntity()) {
      ResetPrismNdfEditor(true);
      m_PrismMaterialGuid.clear();
    }
    m_SelectedEntity = selection.GetEntity();
    return false;
  });
  dispatcher.Dispatch<EntityDeselectedEvent>([this](const auto&) -> bool {
    ResetPrismNdfEditor(true);
    m_PrismMaterialGuid.clear();
    m_SelectedEntity = Engine::InvalidEntityHandle;
    return false;
  });
  dispatcher.Dispatch<SceneChangedEvent>([this](const auto& sceneEvent) -> bool {
    if (m_Scene != sceneEvent.GetScene()) {
      ResetPrismNdfEditor(true);
      m_PrismMaterialGuid.clear();
    }
    m_Scene = sceneEvent.GetScene();
    return false;
  });
  dispatcher.Dispatch<AssetRegistryChangedEvent>([this](const auto& update) -> bool {
    m_Models = update.GetModels();
    m_Textures = update.GetTextures();
    m_Materials = update.GetMaterials();
    m_Environments = update.GetEnvironments();
    return false;
  });
  dispatcher.Dispatch<AssetWorkflowFeedbackEvent>([this](const auto& update) -> bool {
    m_AssetFeedbackSucceeded = update.Succeeded();
    m_AssetFeedback = update.GetMessage();
    return false;
  });
}

void InspectorPanel::Draw() {
  PollPrismGeneration();
  ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);
  if (m_Scene && m_SelectedEntity != Engine::InvalidEntityHandle && m_Scene->World().IsAlive(m_SelectedEntity)) {
    DrawEntityInspector();
  } else {
    ImGui::TextUnformatted("Select an entity from the scene hierarchy.");
  }
  if (!m_AssetFeedback.empty()) {
    ImGui::TextColored(m_AssetFeedbackSucceeded ? ImVec4(0.45F, 0.85F, 0.5F, 1.0F) : ImVec4(0.95F, 0.4F, 0.35F, 1.0F),
                       "%s", m_AssetFeedback.c_str());
  }

  ImGui::End();
}

void InspectorPanel::DrawPrismNdfEditor(Engine::EntityHandle entity, const std::string& materialGuid,
                                        Engine::SceneMaterialDocument& material) {
  const bool contextChanged = m_PrismEditorEntity != entity || m_PrismMaterialGuid != materialGuid;
  if (contextChanged) {
    ResetPrismNdfEditor(true);
    m_PrismEditorEntity = entity;
    m_PrismMaterialGuid = materialGuid;
    if (material.generatedNdf) {
      const auto& saved = *material.generatedNdf;
      const size_t length = (std::min)(saved.prompt.size(), m_PrismPrompt.size() - 1);
      std::copy_n(saved.prompt.data(), length, m_PrismPrompt.data());
      m_PrismDirectional = saved.directional;
      m_PrismPreview = std::make_shared<const Engine::PrismNdfData>(saved.data);
      m_PrismPreviewDirty = true;
      m_PrismPreviewPrompt = saved.prompt;
      m_PrismPreviewDirectional = saved.directional;
      m_PrismPreviewFromSaved = true;
    }
  }
  ImGui::SeparatorText("PRISM NDF generation");
  ImGui::InputTextMultiline("Text prompt", m_PrismPrompt.data(), m_PrismPrompt.size(), ImVec2(-1.0F, 72.0F));
  ImGui::Checkbox("Directional distribution", &m_PrismDirectional);
  ImGui::TextWrapped("Prompt and directional settings remain a preview until you apply the generated NDF.");

  const bool available = m_PrismGeneration.isAvailable && m_PrismGeneration.isAvailable();
  if (!available) {
    ImGui::TextDisabled(
        "PRISM inference is unavailable in this build. Install the packaged model runtime to generate NDFs.");
  }
  if (m_PrismRequestId) {
    const bool cancelling = !m_PrismRequestEligible;
    const char* phase = m_PrismStatus ? PrismPhaseLabel(m_PrismStatus->phase) : "Starting";
    ImGui::Text("%s%s", cancelling ? "Cancelling: " : "Generating: ", phase);
    if (m_PrismStatus && !cancelling) {
      ImGui::ProgressBar(std::clamp(m_PrismStatus->progress, 0.0F, 1.0F), ImVec2(-1.0F, 0.0F));
    }
    if (m_PrismRequestEligible && ImGui::Button("Cancel generation")) {
      if (m_PrismGeneration.cancel) {
        (void)m_PrismGeneration.cancel(*m_PrismRequestId);
      }
      m_PrismRequestEligible = false;
      m_PrismPreview.reset();
      m_PrismPreviewDirty = false;
      m_PrismPreviewFromSaved = false;
    }
  } else {
    const std::string promptText(m_PrismPrompt.data());
    const bool hasPrompt =
        std::any_of(promptText.begin(), promptText.end(), [](unsigned char value) { return !std::isspace(value); });
    ImGui::BeginDisabled(!available || !m_PrismGeneration.start || !hasPrompt);
    if (ImGui::Button("Generate preview")) {
      const std::string prompt(m_PrismPrompt.data());
      auto started = m_PrismGeneration.start(prompt, m_PrismDirectional);
      if (!started) {
        m_PrismStatus = Engine::PrismNdfInferenceStatus{.phase = Engine::PrismNdfInferencePhase::Failed,
                                                        .progress = 1.0F,
                                                        .message = started.error().Message};
      } else {
        m_PrismRequestId = *started;
        m_PrismRequestEntity = entity;
        m_PrismRequestMaterialGuid = materialGuid;
        m_PrismRequestPrompt = prompt;
        m_PrismRequestDirectional = m_PrismDirectional;
        m_PrismRequestEligible = true;
        m_PrismStatus = Engine::PrismNdfInferenceStatus{.requestId = *started};
        m_PrismPreview.reset();
        m_PrismPreviewFromSaved = false;
      }
    }
    ImGui::EndDisabled();
  }
  if (m_PrismStatus && !m_PrismStatus->message.empty()) {
    const bool succeeded = m_PrismStatus->phase != Engine::PrismNdfInferencePhase::Failed;
    ImGui::TextColored(succeeded ? ImVec4(0.45F, 0.85F, 0.5F, 1.0F) : ImVec4(0.95F, 0.4F, 0.35F, 1.0F), "%s",
                       m_PrismStatus->message.c_str());
  }

  if (!m_PrismPreview) {
    return;
  }
  if (m_PrismPreviewDirty) {
    m_PrismPreviewMaximum = 0.0F;
    for (uint32_t y = 0; y < Engine::PrismNdfGridSize; ++y) {
      for (uint32_t x = 0; x < Engine::PrismNdfGridSize; ++x) {
        const float px = 2.0F * static_cast<float>(x) / static_cast<float>(Engine::PrismNdfGridSize - 1) - 1.0F;
        const float py = 2.0F * static_cast<float>(y) / static_cast<float>(Engine::PrismNdfGridSize - 1) - 1.0F;
        const size_t index = static_cast<size_t>(y) * Engine::PrismNdfGridSize + x;
        m_PrismPreviewHeatmap[index] = EvaluatePreviewNdf(*m_PrismPreview, px, py);
        m_PrismPreviewMaximum = (std::max)(m_PrismPreviewMaximum, m_PrismPreviewHeatmap[index]);
      }
    }
    m_PrismPreviewSphereMaximum = 0.0F;
    constexpr std::array<float, 3> light = {0.4242641F, 0.5656854F, 0.7071068F};
    constexpr uint32_t sphereSteps = 64;
    for (uint32_t row = 0; row < sphereSteps; ++row) {
      for (uint32_t column = 0; column < sphereSteps; ++column) {
        const float nx = 2.0F * (static_cast<float>(column) + 0.5F) / static_cast<float>(sphereSteps) - 1.0F;
        const float ny = 1.0F - 2.0F * (static_cast<float>(row) + 0.5F) / static_cast<float>(sphereSteps);
        const float radiusSquared = nx * nx + ny * ny;
        if (radiusSquared > 1.0F) {
          continue;
        }
        const float nz = std::sqrt(1.0F - radiusSquared);
        const float nDotL = (std::max)(0.0F, nx * light[0] + ny * light[1] + nz * light[2]);
        const float ndf = EvaluatePreviewNdf(*m_PrismPreview, nx, ny);
        const float shaded = (0.06F + 0.94F * nDotL) * (0.12F + 0.88F * ndf / (m_PrismPreviewMaximum + 1.0e-6F));
        const size_t index = static_cast<size_t>(row) * sphereSteps + column;
        m_PrismPreviewSphere[index] = shaded;
        m_PrismPreviewSphereMaximum = (std::max)(m_PrismPreviewSphereMaximum, shaded);
      }
    }
    m_PrismPreviewDirty = false;
  }
  ImGui::TextUnformatted("NDF heatmap");
  const float cell = 3.0F;
  const ImVec2 heatmapOrigin = ImGui::GetCursorScreenPos();
  const ImVec2 heatmapSize(cell * Engine::PrismNdfGridSize, cell * Engine::PrismNdfGridSize);
  ImGui::InvisibleButton("##PRISM NDF heatmap", heatmapSize);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  for (uint32_t y = 0; y < Engine::PrismNdfGridSize; ++y) {
    for (uint32_t x = 0; x < Engine::PrismNdfGridSize; ++x) {
      const size_t index = static_cast<size_t>(y) * Engine::PrismNdfGridSize + x;
      const ImVec2 min(heatmapOrigin.x + cell * static_cast<float>(x), heatmapOrigin.y + cell * static_cast<float>(y));
      draw->AddRectFilled(min, ImVec2(min.x + cell, min.y + cell),
                          HeatColor(m_PrismPreviewHeatmap[index], m_PrismPreviewMaximum));
    }
  }

  ImGui::TextUnformatted("Lit sphere preview");
  const float sphereSize = 192.0F;
  const ImVec2 sphereOrigin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##PRISM NDF lit sphere", ImVec2(sphereSize, sphereSize));
  constexpr uint32_t sphereSteps = 64;
  const float sphereCell = sphereSize / static_cast<float>(sphereSteps);
  for (uint32_t row = 0; row < sphereSteps; ++row) {
    for (uint32_t column = 0; column < sphereSteps; ++column) {
      const size_t index = static_cast<size_t>(row) * sphereSteps + column;
      if (m_PrismPreviewSphere[index] <= 0.0F) {
        continue;
      }
      const ImVec2 min(sphereOrigin.x + sphereCell * static_cast<float>(column),
                       sphereOrigin.y + sphereCell * static_cast<float>(row));
      draw->AddRectFilled(min, ImVec2(min.x + sphereCell + 0.5F, min.y + sphereCell + 0.5F),
                          HeatColor(m_PrismPreviewSphere[index], m_PrismPreviewSphereMaximum));
    }
  }
  ImGui::TextWrapped("Prompt: %s%s", m_PrismPreviewPrompt.c_str(),
                     m_PrismPreviewDirectional ? " (directional)" : " (isotropic)");
  if ((!m_PrismPreviewFromSaved || material.brdfModel != "prismNdf") && !materialGuid.empty()) {
    if (ImGui::Button("Apply to material")) {
      Engine::SceneGeneratedNdfDocument generated;
      generated.data = *m_PrismPreview;
      generated.directional = m_PrismPreviewDirectional;
      generated.prompt = m_PrismPreviewPrompt;
      generated.modelVersion = "PRISM release model";
      generated.exporterVersion = "PRISM grid / degree-8 Zernike fit";
      EmitEvent(std::make_unique<PrismNdfApplyRequestedEvent>(entity, materialGuid, std::move(generated)));
      if (m_AssetFeedbackSucceeded) {
        m_PrismEditorRequested = false;
        m_PrismPreviewFromSaved = true;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard preview")) {
      if (m_PrismRequestId && m_PrismGeneration.cancel) {
        (void)m_PrismGeneration.cancel(*m_PrismRequestId);
        m_PrismRequestEligible = false;
      }
      m_PrismPreview.reset();
      m_PrismPreviewFromSaved = false;
      m_PrismStatus.reset();
      if (material.generatedNdf) {
        const auto& saved = *material.generatedNdf;
        m_PrismPreview = std::make_shared<const Engine::PrismNdfData>(saved.data);
        m_PrismPreviewDirty = true;
        m_PrismPreviewPrompt = saved.prompt;
        m_PrismPreviewDirectional = saved.directional;
        m_PrismPreviewFromSaved = true;
      } else {
        m_PrismEditorRequested = false;
      }
    }
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void InspectorPanel::DrawEntityInspector() {
  const Engine::ArchetypeWorld& world = m_Scene->World();
  const Engine::EntityHandle entity = m_SelectedEntity;
  const bool isSceneRoot = m_Scene->IsSceneRoot(entity);
  const auto emitSet = [this, entity](Mutation::Component type, auto value) -> void {
    EmitEvent(std::make_unique<Mutation>(entity, Mutation::Operation::Set, type, Mutation::Payload{std::move(value)}));
  };
  const auto emitAdd = [this, entity](Mutation::Component type) -> void {
    EmitEvent(std::make_unique<Mutation>(entity, Mutation::Operation::Add, type));
  };
  const auto emitRemove = [this, entity](Mutation::Component type) -> void {
    EmitEvent(std::make_unique<Mutation>(entity, Mutation::Operation::Remove, type));
  };

  const std::string entityId = "ID: " + std::to_string(entity.Index) + ":" + std::to_string(entity.Generation);
  ImGui::TextUnformatted(entityId.c_str());
  ImGui::Separator();
  ImGui::PushID(entityId.c_str());
  if (const auto* storedName = world.Get<Engine::Ecs::Name>(entity);
      storedName != nullptr && ImGui::CollapsingHeader("Name", ImGuiTreeNodeFlags_DefaultOpen)) {
    Engine::Ecs::Name name = *storedName;
    std::array<char, 256> buffer{};
    const size_t length = (std::min)(name.Value.size(), buffer.size() - 1);
    std::copy_n(name.Value.data(), length, buffer.data());
    if (isSceneRoot) {
      ImGui::BeginDisabled();
      (void)ImGui::InputText(Editor::Panels::Detail::kEntityNameInputLabel, buffer.data(), buffer.size());
      ImGui::EndDisabled();
    } else if (ImGui::InputText(Editor::Panels::Detail::kEntityNameInputLabel, buffer.data(), buffer.size())) {
      name.Value = buffer.data();
      emitSet(Mutation::Component::Name, std::move(name));
    }
  }
  ImGui::PopID();

  if (isSceneRoot) {
    const auto* storedSkybox = world.Get<Engine::Ecs::Skybox>(entity);
    if (storedSkybox == nullptr) {
      if (ImGui::Button("Add Skybox")) {
        emitAdd(Mutation::Component::Skybox);
      }
      ImGui::SameLine();
      ImGui::TextDisabled("Add a scene-wide sky background.");
    } else if (DrawComponentHeader("Skybox", "Skybox", "Skybox component header", Mutation::Component::Skybox,
                                   emitRemove)) {
      Engine::Ecs::Skybox skybox = *storedSkybox;
      bool changed = false;
      const char* modeName = skybox.Mode == Engine::Ecs::SkyboxMode::LdrPanorama  ? "PNG panorama"
                             : skybox.Mode == Engine::Ecs::SkyboxMode::SolidColor ? "Solid color"
                                                                                  : "HDR environment";
      if (ImGui::BeginCombo("Background", modeName)) {
        const auto selectMode = [&](Engine::Ecs::SkyboxMode mode, const char* label) -> void {
          const bool selected = skybox.Mode == mode;
          if (ImGui::Selectable(label, selected) && !selected) {
            skybox.Mode = mode;
            if (mode != Engine::Ecs::SkyboxMode::HdrEnvironment) {
              skybox.EnvironmentGuid.clear();
            }
            if (mode != Engine::Ecs::SkyboxMode::LdrPanorama) {
              skybox.TextureGuid.clear();
            }
            changed = true;
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        };
        selectMode(Engine::Ecs::SkyboxMode::HdrEnvironment, "HDR environment");
        selectMode(Engine::Ecs::SkyboxMode::LdrPanorama, "PNG panorama");
        selectMode(Engine::Ecs::SkyboxMode::SolidColor, "Solid color");
        ImGui::EndCombo();
      }
      if (skybox.Mode == Engine::Ecs::SkyboxMode::HdrEnvironment) {
        const auto selected = std::ranges::find(m_Environments, skybox.EnvironmentGuid,
                                                &Engine::AssetManager::EnvironmentAssetInfo::guid);
        const std::string preview = skybox.EnvironmentGuid.empty() ? "None"
                                    : selected != m_Environments.end()
                                        ? selected->name
                                        : "Missing environment: " + skybox.EnvironmentGuid;
        if (ImGui::BeginCombo("HDR environment asset", preview.c_str())) {
          const bool noEnvironmentSelected = skybox.EnvironmentGuid.empty();
          if (ImGui::Selectable("None", noEnvironmentSelected) && !noEnvironmentSelected) {
            skybox.EnvironmentGuid.clear();
            changed = true;
          }
          for (const auto& environment : m_Environments) {
            ImGui::PushID(environment.guid.c_str());
            const bool isSelected = skybox.EnvironmentGuid == environment.guid;
            if (ImGui::Selectable(environment.name.c_str(), isSelected) && !isSelected) {
              skybox.EnvironmentGuid = environment.guid;
              changed = true;
            }
            if (isSelected) {
              ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
          }
          ImGui::EndCombo();
        }
        if (m_Environments.empty()) {
          ImGui::TextDisabled("Import a .hdr environment from the Assets panel to use it here.");
        }
        changed |= ImGui::Checkbox("Enable HDRI lighting", &skybox.EnableEnvironmentLighting);
        changed |= ImGui::SliderFloat("Environment intensity", &skybox.Intensity, 0.0F, 10.0F, "%.2f");
      } else if (skybox.Mode == Engine::Ecs::SkyboxMode::LdrPanorama) {
        const auto selected =
            std::ranges::find(m_Textures, skybox.TextureGuid, &Engine::AssetManager::TextureAssetInfo::guid);
        const std::string preview = skybox.TextureGuid.empty()     ? "None"
                                    : selected != m_Textures.end() ? selected->name
                                                                   : "Missing texture: " + skybox.TextureGuid;
        if (ImGui::BeginCombo("PNG panorama asset", preview.c_str())) {
          const bool noTextureSelected = skybox.TextureGuid.empty();
          if (ImGui::Selectable("None", noTextureSelected) && !noTextureSelected) {
            skybox.TextureGuid.clear();
            changed = true;
          }
          for (const auto& texture : m_Textures) {
            if (texture.sourcePath.extension() != ".png") {
              continue;
            }
            ImGui::PushID(texture.guid.c_str());
            const bool isSelected = skybox.TextureGuid == texture.guid;
            if (ImGui::Selectable(texture.name.c_str(), isSelected) && !isSelected) {
              skybox.TextureGuid = texture.guid;
              changed = true;
            }
            if (isSelected) {
              ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
          }
          ImGui::EndCombo();
        }
        if (m_Textures.empty()) {
          ImGui::TextDisabled("Import a PNG texture from the Assets panel to use it here.");
        }
      } else {
        changed |= ImGui::ColorEdit3("Sky color (linear RGB)", skybox.Color.data());
      }
      if (changed) {
        emitSet(Mutation::Component::Skybox, std::move(skybox));
      }
    }
  }

  if (isSceneRoot && ImGui::CollapsingHeader("Scene Ambient Light", ImGuiTreeNodeFlags_DefaultOpen)) {
    Engine::AmbientLightSettings ambientLight = m_Scene->AmbientLight();
    bool changed = ImGui::Checkbox("Enabled", &ambientLight.Enabled);
    if (ambientLight.Enabled) {
      ImGui::Indent();
      changed |= ImGui::ColorEdit3("Color (linear RGB)", ambientLight.Color.data());
      changed |= ImGui::SliderFloat("Ambient light intensity", &ambientLight.Intensity, 0.0F, 10.0F, "%.2f");
      ImGui::Unindent();
    }
    if (changed) {
      EmitEvent(std::make_unique<SceneAmbientLightChangedEvent>(ambientLight));
    }
  }

  if (!isSceneRoot) {
    if (const auto* storedTransform = world.Get<Engine::Ecs::LocalTransform>(entity);
        storedTransform != nullptr && ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
      Engine::Ecs::LocalTransform transform = *storedTransform;
      bool changed = ImGui::DragFloat3("Local position", transform.Position.data(), 0.05F);
      std::array<float, 3> anglesDegrees = Editor::TransformRotation::ToPitchYawRollDegrees(transform.Rotation);
      if (ImGui::DragFloat3("Pitch / Yaw / Roll", anglesDegrees.data(), 0.25F, -360.0F, 360.0F, "%.1f deg")) {
        transform.Rotation = Editor::TransformRotation::FromPitchYawRollDegrees(anglesDegrees);
        changed = true;
      }
      if (ImGui::TreeNode("Quaternion (advanced)")) {
        changed |= ImGui::DragFloat4("Raw quaternion", transform.Rotation.data(), 0.01F);
        ImGui::TreePop();
      }
      const std::array<float, 3> previousScale = transform.Scale;
      const float lockWidth = ImGui::CalcTextSize("Lock").x + ImGui::GetFrameHeight();
      ImGui::SetNextItemWidth(-lockWidth);
      if (ImGui::DragFloat3("Local scale", transform.Scale.data(), 0.05F, 0.01F, 1000.0F)) {
        (void)Editor::TransformScaleLock::Apply(transform.Scale, previousScale, m_LockTransformScale);
        changed = true;
      }
      ImGui::SameLine();
      (void)ImGui::Checkbox("Lock", &m_LockTransformScale);
      changed |= ImGui::Checkbox("Flip horizontal", &transform.FlipHorizontal);
      changed |= ImGui::Checkbox("Flip vertical", &transform.FlipVertical);
      if (changed) {
        emitSet(Mutation::Component::Transform, transform);
      }
    }

    if (const auto* storedVisibility = world.Get<Engine::Ecs::LocalVisibility>(entity);
        storedVisibility != nullptr && ImGui::CollapsingHeader("Visibility")) {
      Engine::Ecs::LocalVisibility visibility = *storedVisibility;
      if (ImGui::Checkbox("Visible in scene", &visibility.IsVisible)) {
        emitSet(Mutation::Component::LocalVisibility, visibility);
      }
    }
  }

  if (!world.Has<Engine::Ecs::MeshRenderer>(entity)) {
    m_MeshSourceSelectionEntity = Engine::InvalidEntityHandle;
  }
  if (const auto* storedMesh = world.Get<Engine::Ecs::MeshRenderer>(entity);
      storedMesh != nullptr &&
      DrawComponentHeader("Mesh", "Mesh", "Mesh component header", Mutation::Component::MeshRenderer, emitRemove)) {
    Engine::Ecs::MeshRenderer mesh = *storedMesh;
    const char* sourceNames[] = {"Asset model", "Primitive"};
    if (m_MeshSourceSelectionEntity != entity) {
      m_MeshSourceSelectionEntity = entity;
      m_MeshSourceSelectionIndex = mesh.PrimitiveType.empty() ? 0 : 1;
    }
    int sourceIndex = m_MeshSourceSelectionIndex;
    bool changed = false;
    bool materialChanged = false;
    if (ImGui::Combo("Mesh source", &sourceIndex, sourceNames, 2)) {
      m_MeshSourceSelectionIndex = sourceIndex;
      if (sourceIndex == 1 && mesh.PrimitiveType.empty()) {
        mesh.ModelGuid.clear();
        mesh.PrimitiveType = "cube";
        changed = true;
      }
    }
    if (sourceIndex == 0) {
      const auto selected = std::ranges::find(m_Models, mesh.ModelGuid, &Engine::AssetManager::ModelAssetInfo::guid);
      const char* preview = selected == m_Models.end() ? "Select model..." : selected->name.c_str();
      const bool modelPickerOpen = ImGui::BeginCombo("Model asset", preview);
      // The combo itself is the drop target, so models can be dropped directly
      // onto the picker without a separate placeholder button.
      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ModelAssetDragDropType);
            payload != nullptr && payload->IsDelivery()) {
          const auto guid = ReadAssetDragDropGuid(payload->Data, payload->DataSize);
          std::string error;
          if (!guid || !AssignDroppedModel(mesh, guid.value_or(std::string{}), m_Models, error)) {
            m_AssetFeedbackSucceeded = false;
            m_AssetFeedback = guid ? error : "Dropped model payload is malformed";
          } else {
            m_MeshSourceSelectionIndex = 0;
            m_AssetFeedbackSucceeded = true;
            m_AssetFeedback = "Model assigned";
            changed = true;
          }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(TextureAssetDragDropType);
            payload != nullptr && payload->IsDelivery()) {
          m_AssetFeedbackSucceeded = false;
          m_AssetFeedback = "Textures can only be dropped on a material texture slot";
        }
        ImGui::EndDragDropTarget();
      }
      if (modelPickerOpen) {
        for (const auto& model : m_Models) {
          const bool isSelected = model.guid == mesh.ModelGuid;
          if (ImGui::Selectable(model.name.c_str(), isSelected)) {
            mesh.ModelGuid = model.guid;
            mesh.PrimitiveType.clear();
            m_MeshSourceSelectionIndex = 0;
            changed = true;
          }
        }
        ImGui::EndCombo();
      }
    } else {
      constexpr std::array<const char*, 4> primitiveNames = {"Cube", "Sphere", "Plane", "Cone"};
      constexpr std::array<const char*, 4> primitiveValues = {"cube", "sphere", "plane", "cone"};
      size_t primitiveIndex = 0;
      for (size_t index = 0; index < primitiveValues.size(); ++index) {
        if (mesh.PrimitiveType == primitiveValues[index]) {
          primitiveIndex = index;
          break;
        }
      }
      if (ImGui::BeginCombo("Primitive", primitiveNames[primitiveIndex])) {
        for (size_t index = 0; index < primitiveNames.size(); ++index) {
          const bool isSelected = primitiveIndex == index;
          if (ImGui::Selectable(primitiveNames[index], isSelected)) {
            mesh.ModelGuid.clear();
            mesh.PrimitiveType = primitiveValues[index];
            m_MeshSourceSelectionIndex = 1;
            changed = true;
          }
        }
        ImGui::EndCombo();
      }
    }
    changed |= ImGui::Checkbox("Visible", &mesh.Visible);
    changed |= ImGui::Checkbox("Cast shadows", &mesh.CastShadows);
    changed |= ImGui::Checkbox("Receive shadows", &mesh.ReceiveShadows);
    changed |= ImGui::Checkbox("Wireframe", &mesh.Wireframe);
    changed |= ImGui::Checkbox("Disable depth test", &mesh.DisableDepthTest);
    const bool legacyEmbeddedMaterial = mesh.Material.has_value() && mesh.MaterialGuid.empty();
    const auto selectedMaterial =
        std::ranges::find(m_Materials, mesh.MaterialGuid, &Engine::AssetManager::MaterialAssetInfo::guid);
    const char* materialPreview = "Default";
    if (legacyEmbeddedMaterial) {
      materialPreview = "Embedded (legacy)";
    } else if (!mesh.MaterialGuid.empty()) {
      materialPreview =
          selectedMaterial == m_Materials.end() ? "Missing material asset" : selectedMaterial->name.c_str();
    }
    if (ImGui::BeginCombo("Material", materialPreview)) {
      if (ImGui::Selectable("Default", !legacyEmbeddedMaterial && mesh.MaterialGuid.empty())) {
        UseDefaultMaterial(mesh);
        changed = true;
      }
      if (legacyEmbeddedMaterial) {
        ImGui::BeginDisabled();
        (void)ImGui::Selectable("Embedded (legacy)", true);
        ImGui::EndDisabled();
      }
      for (const auto& materialAsset : m_Materials) {
        const bool isSelected = materialAsset.guid == mesh.MaterialGuid;
        if (ImGui::Selectable(materialAsset.name.c_str(), isSelected)) {
          EmitEvent(std::make_unique<MaterialAssetSelectionRequestedEvent>(entity, materialAsset.guid));
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Create Custom")) {
      EmitEvent(std::make_unique<AssetImportRequestedEvent>(AssetImportRequestedEvent::AssetType::Material, entity));
    }
    if (mesh.Material) {
      auto& material = *mesh.Material;
      ImGui::SeparatorText("Material");
      const auto drawTextureTarget = [this, &material, &changed,
                                      &materialChanged](const char* label, MaterialTextureSlot slot,
                                                        const std::optional<std::string>& currentGuid) -> void {
        std::string targetLabel = std::string(label) + ": Drop texture here";
        if (currentGuid) {
          const auto texture =
              std::ranges::find(m_Textures, *currentGuid, &Engine::AssetManager::TextureAssetInfo::guid);
          targetLabel = std::string(label) + ": " + (texture == m_Textures.end() ? *currentGuid : texture->name);
        }
        ImGui::Button(targetLabel.c_str(), ImVec2(-1.0F, 0.0F));
        if (!ImGui::BeginDragDropTarget()) {
          return;
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(TextureAssetDragDropType);
            payload != nullptr && payload->IsDelivery()) {
          const auto guid = ReadAssetDragDropGuid(payload->Data, payload->DataSize);
          std::string error;
          if (!guid || !AssignDroppedTexture(material, slot, guid.value_or(std::string{}), m_Textures, error)) {
            m_AssetFeedbackSucceeded = false;
            m_AssetFeedback = guid ? error : "Dropped texture payload is malformed";
          } else {
            changed = true;
            materialChanged = true;
            m_AssetFeedbackSucceeded = true;
            m_AssetFeedback = std::string(label) + " texture assigned";
          }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ModelAssetDragDropType);
            payload != nullptr && payload->IsDelivery()) {
          m_AssetFeedbackSucceeded = false;
          m_AssetFeedback = "Models cannot be assigned to material texture slots";
        }
        ImGui::EndDragDropTarget();
      };
      const auto drawTextureAnisotropy = [&material, &materialChanged](const char* label, size_t slot) -> void {
        constexpr std::array<uint32_t, 6> levels = {0, 1, 2, 4, 8, 16};
        constexpr std::array<const char*, 6> labels = {"Global cap", "1x", "2x", "4x", "8x", "16x"};
        const auto current = material.textureAnisotropyLevels[slot];
        const auto found = std::ranges::find(levels, current);
        int selection = found == levels.end() ? 0 : static_cast<int>(std::distance(levels.begin(), found));
        if (ImGui::Combo(label, &selection, labels.data(), static_cast<int>(labels.size()))) {
          material.textureAnisotropyLevels[slot] = levels[static_cast<size_t>(selection)];
          materialChanged = true;
        }
      };
      ImGui::SeparatorText("General Material Properties");
      materialChanged |= ImGui::ColorEdit4("Base color factor", material.baseColor.data());
      drawTextureTarget("Color texture", MaterialTextureSlot::BaseColor, material.baseColorTextureGuid);
      drawTextureAnisotropy("Color texture anisotropy", 0);
      drawTextureTarget("Normal texture", MaterialTextureSlot::Normal, material.normalTextureGuid);
      drawTextureAnisotropy("Normal texture anisotropy", 1);
      if (ImGui::TreeNode("Other texture filtering")) {
        drawTextureAnisotropy("Metallic-roughness texture anisotropy", 2);
        drawTextureAnisotropy("Occlusion texture anisotropy", 3);
        drawTextureAnisotropy("Emissive texture anisotropy", 4);
        ImGui::TreePop();
      }
      materialChanged |= ImGui::DragFloat("Normal map strength", &material.normalScale, 0.01F, 0.0F, 10.0F);
      if (ImGui::TreeNode("UV Settings")) {
        int baseColorTexCoord = static_cast<int>(material.baseColorTexCoord);
        if (ImGui::DragInt("Color UV set", &baseColorTexCoord, 0.1F, 0, 7)) {
          material.baseColorTexCoord = static_cast<uint32_t>(std::clamp(baseColorTexCoord, 0, 7));
          materialChanged = true;
        }
        materialChanged |=
            ImGui::DragFloat2("Color UV offset", material.baseColorTextureTransform.offset.data(), 0.01F);
        materialChanged |= ImGui::DragFloat2("Color UV scale", material.baseColorTextureTransform.scale.data(), 0.01F);
        materialChanged |= ImGui::DragFloat("Color UV rotation", &material.baseColorTextureTransform.rotation, 0.01F);

        int normalTexCoord = static_cast<int>(material.normalTexCoord);
        if (ImGui::DragInt("Normal UV set", &normalTexCoord, 0.1F, 0, 7)) {
          material.normalTexCoord = static_cast<uint32_t>(std::clamp(normalTexCoord, 0, 7));
          materialChanged = true;
        }
        materialChanged |= ImGui::DragFloat2("Normal UV offset", material.normalTextureTransform.offset.data(), 0.01F);
        materialChanged |= ImGui::DragFloat2("Normal UV scale", material.normalTextureTransform.scale.data(), 0.01F);
        materialChanged |= ImGui::DragFloat("Normal UV rotation", &material.normalTextureTransform.rotation, 0.01F);
        ImGui::TreePop();
      }
      materialChanged |= ImGui::SliderFloat("Metallic", &material.metallic, 0.0F, 1.0F);
      materialChanged |= ImGui::SliderFloat("Roughness", &material.roughness, 0.0F, 1.0F);
      materialChanged |= ImGui::ColorEdit3("Emissive", material.emissive.data());
      constexpr const char* workflowNames[] = {"Metallic-Roughness", "Specular-Glossiness"};
      int workflowIndex = material.workflow == "specularGlossiness" ? 1 : 0;
      if (ImGui::Combo("Workflow", &workflowIndex, workflowNames, 2)) {
        material.workflow = workflowIndex == 1 ? "specularGlossiness" : "metallicRoughness";
        materialChanged = true;
      }
      constexpr const char* specularModeNames[] = {"Index of refraction", "Explicit F0"};
      int specularModeIndex = material.specularMode == "explicitF0" ? 1 : 0;
      if (ImGui::Combo("Dielectric specular", &specularModeIndex, specularModeNames, 2)) {
        material.specularMode = specularModeIndex == 1 ? "explicitF0" : "ior";
        materialChanged = true;
      }
      if (material.specularMode == "explicitF0") {
        materialChanged |= ImGui::ColorEdit3("Specular F0", material.specularF0.data());
      } else {
        materialChanged |= ImGui::SliderFloat("Index of refraction", &material.ior, 1.0F, 3.0F, "%.2f");
      }
      ImGui::SeparatorText("MDF Parameters");
      constexpr const char* brdfNames[] = {"GGX",     "GTR",         "Beckmann",
                                           "Gauss",   "Blinn-Phong", "Ashikhmin-Shirley",
                                           "Schlick", "Sawicki",     "PRISM generated NDF"};
      const bool stagedPrism =
          m_PrismEditorRequested && m_PrismEditorEntity == entity && m_PrismMaterialGuid == mesh.MaterialGuid;
      int brdfIndex = stagedPrism                                ? 8
                      : material.brdfModel == "gtr"              ? 1
                      : material.brdfModel == "beckmann"         ? 2
                      : material.brdfModel == "gauss"            ? 3
                      : material.brdfModel == "blinnPhong"       ? 4
                      : material.brdfModel == "ashikhminShirley" ? 5
                      : material.brdfModel == "schlick"          ? 6
                      : material.brdfModel == "sawicki"          ? 7
                      : material.brdfModel == "prismNdf"         ? 8
                                                                 : 0;
      if (ImGui::Combo("BRDF distribution", &brdfIndex, brdfNames, 9)) {
        if (brdfIndex == 8 && (legacyEmbeddedMaterial || mesh.MaterialGuid.empty())) {
          m_AssetFeedbackSucceeded = false;
          m_AssetFeedback = "PRISM generation requires a reusable material asset";
        } else if (brdfIndex == 8) {
          m_PrismEditorRequested = true;
          m_PrismEditorEntity = entity;
          m_PrismMaterialGuid = mesh.MaterialGuid;
        } else {
          if (brdfIndex != 8) {
            ResetPrismNdfEditor(true);
            m_PrismEditorEntity = entity;
            m_PrismMaterialGuid = mesh.MaterialGuid;
          }
          material.brdfModel = brdfIndex == 1   ? "gtr"
                               : brdfIndex == 2 ? "beckmann"
                               : brdfIndex == 3 ? "gauss"
                               : brdfIndex == 4 ? "blinnPhong"
                               : brdfIndex == 5 ? "ashikhminShirley"
                               : brdfIndex == 6 ? "schlick"
                               : brdfIndex == 7 ? "sawicki"
                               : brdfIndex == 8 ? "prismNdf"
                                                : "ggx";
          materialChanged = true;
        }
      }
      if ((material.brdfModel == "prismNdf" || m_PrismEditorRequested) && !legacyEmbeddedMaterial &&
          !mesh.MaterialGuid.empty()) {
        DrawPrismNdfEditor(entity, mesh.MaterialGuid, material);
      }
      if (material.brdfModel == "gtr") {
        materialChanged |= ImGui::SliderFloat("GTR gamma", &material.gtrGamma, 1.0F, 8.0F, "%.2f");
      }
      if (material.brdfModel == "ashikhminShirley") {
        materialChanged |= ImGui::SliderFloat("Roughness U", &material.anisotropicRoughnessU, 0.0f, 1.0f);
        materialChanged |= ImGui::SliderFloat("Roughness V", &material.anisotropicRoughnessV, 0.0f, 1.0f);
      }
      constexpr const char* alphaNames[] = {"Opaque", "Mask", "Blend"};
      int alphaIndex = material.alphaMode == "mask" ? 1 : material.alphaMode == "blend" ? 2 : 0;
      if (ImGui::Combo("Alpha mode", &alphaIndex, alphaNames, 3)) {
        material.alphaMode = alphaIndex == 1 ? "mask" : alphaIndex == 2 ? "blend" : "opaque";
        materialChanged = true;
      }
      if (material.alphaMode == "mask") {
        materialChanged |= ImGui::SliderFloat("Alpha cutoff", &material.alphaCutoff, 0.0F, 1.0F);
      }
      materialChanged |= ImGui::Checkbox("Double sided", &material.doubleSided);
      changed |= materialChanged;
    }
    if (materialChanged && !legacyEmbeddedMaterial && !mesh.MaterialGuid.empty() && mesh.Material.has_value()) {
      EmitEvent(std::make_unique<MaterialAssetEditRequestedEvent>(mesh.MaterialGuid, *mesh.Material));
    }
    if (changed) {
      emitSet(Mutation::Component::MeshRenderer, std::move(mesh));
    }
  }

  if (const auto* storedLight = world.Get<Engine::Ecs::Light>(entity);
      storedLight != nullptr &&
      DrawComponentHeader("Light", "Light", "Light component header", Mutation::Component::SpotLight, emitRemove)) {
    Engine::Ecs::Light light = *storedLight;
    constexpr const char* lightTypeNames[] = {"Directional", "Point", "Spot"};
    int typeIndex = static_cast<int>(light.Type);
    bool changed = ImGui::Combo("Type", &typeIndex, lightTypeNames, 3);
    if (changed) {
      light.Type = static_cast<Engine::Ecs::LightType>(typeIndex);
    }
    if (light.Type != Engine::Ecs::LightType::Point) {
      changed |= ImGui::DragFloat3("Direction", light.Direction.data(), 0.01F, -1.0F, 1.0F);
    }
    changed |= ImGui::ColorEdit3("Light color", light.Color.data());
    changed |= ImGui::DragFloat("Intensity", &light.Intensity, 0.05F, 0.0F, 100000.0F);
    if (light.Type != Engine::Ecs::LightType::Directional) {
      changed |= ImGui::DragFloat("Range", &light.Range, 0.05F, 0.01F, 100000.0F);
    }
    if (light.Type == Engine::Ecs::LightType::Spot) {
      changed |= ImGui::DragFloat("Inner cone", &light.InnerAngleDegrees, 0.1F, 0.0F, 88.9F);
      changed |= ImGui::DragFloat("Outer cone", &light.OuterAngleDegrees, 0.1F, light.InnerAngleDegrees + 0.1F, 89.9F);
    }
    changed |= ImGui::DragFloat("Shadow bias", &light.ShadowBias, 0.0001F, 0.0F, 0.1F);
    changed |= ImGui::DragFloat("Shadow normal bias", &light.ShadowNormalBias, 0.0001F, 0.0F, 0.1F);
    changed |= ImGui::Checkbox("Cast shadows", &light.CastShadows);
    if (changed) {
      emitSet(Mutation::Component::SpotLight, light);
    }
  }

  if (const auto* storedCamera = world.Get<Engine::Ecs::Camera>(entity);
      storedCamera != nullptr &&
      DrawComponentHeader("Camera", "Camera", "Camera component header", Mutation::Component::Camera, emitRemove)) {
    Engine::Ecs::Camera camera = *storedCamera;
    bool changed = ImGui::SliderFloat("Field of view", &camera.FieldOfViewDegrees, 10.0F, 150.0F, "%.1f deg");
    changed |= ImGui::DragFloat("Near plane", &camera.NearPlane, 0.01F, 0.001F, 100.0F);
    changed |= ImGui::DragFloat("Far plane", &camera.FarPlane, 0.1F, camera.NearPlane + 0.01F, 100000.0F);
    constexpr std::array<const char*, 5> resolutionOptions = {"720p (1280 x 720)", "1080p (1920 x 1080)",
                                                              "1440p (2560 x 1440)", "4K UHD (3840 x 2160)", "Custom"};
    int resolutionIndex = 4;
    if (camera.OutputWidth == 1280 && camera.OutputHeight == 720) {
      resolutionIndex = 0;
    } else if (camera.OutputWidth == 1920 && camera.OutputHeight == 1080) {
      resolutionIndex = 1;
    } else if (camera.OutputWidth == 2560 && camera.OutputHeight == 1440) {
      resolutionIndex = 2;
    } else if (camera.OutputWidth == 3840 && camera.OutputHeight == 2160) {
      resolutionIndex = 3;
    } else if (m_CustomResolutionEntity == entity || (camera.OutputWidth != 0 && camera.OutputHeight != 0)) {
      resolutionIndex = 4;
    }
    if (ImGui::Combo("Output resolution", &resolutionIndex, resolutionOptions.data(),
                     static_cast<int>(resolutionOptions.size()))) {
      switch (resolutionIndex) {
      case 0:
        m_CustomResolutionEntity = Engine::InvalidEntityHandle;
        camera.OutputWidth = 1280;
        camera.OutputHeight = 720;
        changed = true;
        break;
      case 1:
        m_CustomResolutionEntity = Engine::InvalidEntityHandle;
        camera.OutputWidth = 1920;
        camera.OutputHeight = 1080;
        changed = true;
        break;
      case 2:
        m_CustomResolutionEntity = Engine::InvalidEntityHandle;
        camera.OutputWidth = 2560;
        camera.OutputHeight = 1440;
        changed = true;
        break;
      case 3:
        m_CustomResolutionEntity = Engine::InvalidEntityHandle;
        camera.OutputWidth = 3840;
        camera.OutputHeight = 2160;
        changed = true;
        break;
      default:
        m_CustomResolutionEntity = entity;
        break;
      }
    }
    if (resolutionIndex == 4) {
      int outputWidth =
          static_cast<int>((std::min)(camera.OutputWidth, static_cast<uint32_t>(std::numeric_limits<int>::max())));
      int outputHeight =
          static_cast<int>((std::min)(camera.OutputHeight, static_cast<uint32_t>(std::numeric_limits<int>::max())));
      const bool widthChanged = ImGui::InputInt("Output width", &outputWidth);
      const bool heightChanged = ImGui::InputInt("Output height", &outputHeight);
      if (widthChanged && outputWidth > 0) {
        camera.OutputWidth = static_cast<uint32_t>(outputWidth);
        changed = true;
      }
      if (heightChanged && outputHeight > 0) {
        camera.OutputHeight = static_cast<uint32_t>(outputHeight);
        changed = true;
      }
    }
    changed |= ImGui::InputInt("Priority", &camera.Priority);
    changed |= ImGui::Checkbox("Active camera", &camera.Active);
    bool overrideAmbientLight = camera.AmbientLightOverride.has_value();
    if (ImGui::Checkbox("Override scene ambient light", &overrideAmbientLight)) {
      if (overrideAmbientLight) {
        camera.AmbientLightOverride = m_Scene->AmbientLight();
      } else {
        camera.AmbientLightOverride.reset();
      }
      changed = true;
    }
    if (camera.AmbientLightOverride) {
      ImGui::Indent();
      changed |= ImGui::Checkbox("Ambient light enabled", &camera.AmbientLightOverride->Enabled);
      if (camera.AmbientLightOverride->Enabled) {
        changed |= ImGui::ColorEdit3("Ambient color (linear RGB)", camera.AmbientLightOverride->Color.data());
        changed |=
            ImGui::SliderFloat("Ambient intensity", &camera.AmbientLightOverride->Intensity, 0.0F, 10.0F, "%.2f");
      }
      ImGui::Unindent();
    } else {
      ImGui::TextDisabled("Ambient light follows the scene setting.");
    }
    if (changed) {
      emitSet(Mutation::Component::Camera, camera);
    }
  }

  if (const auto* storedPostProcessing = world.Get<Engine::Ecs::CameraPostProcessing>(entity);
      storedPostProcessing != nullptr &&
      DrawComponentHeader("Post Processing", "Post Processing", "Camera post-processing component header",
                          Mutation::Component::CameraPostProcessing, emitRemove)) {
    Engine::Ecs::CameraPostProcessing postProcessing = *storedPostProcessing;
    bool changed = false;
    if (postProcessing.Effects.empty() && !postProcessing.AmbientOcclusion) {
      ImGui::TextWrapped("No effects are active. The camera renders without post-processing.");
    }
    bool ambientOcclusionEnabled = postProcessing.AmbientOcclusion.has_value();
    if (ImGui::Checkbox("Screen-space ambient occlusion", &ambientOcclusionEnabled)) {
      if (ambientOcclusionEnabled) {
        postProcessing.AmbientOcclusion.emplace();
      } else {
        postProcessing.AmbientOcclusion.reset();
      }
      changed = true;
    }
    if (postProcessing.AmbientOcclusion) {
      ImGui::Indent();
      changed |=
          ImGui::SliderFloat("AO Radius", &postProcessing.AmbientOcclusion->Radius, 0.0F, 5.0F, "%.2f world units");
      changed |= ImGui::SliderFloat("AO Intensity", &postProcessing.AmbientOcclusion->Intensity, 0.0F, 4.0F, "%.2f");
      static constexpr const char* sampleCounts[] = {"8", "16", "32"};
      int sampleCountIndex = postProcessing.AmbientOcclusion->SampleCount == 8U    ? 0
                             : postProcessing.AmbientOcclusion->SampleCount == 32U ? 2
                                                                                   : 1;
      if (ImGui::Combo("Quality (samples)", &sampleCountIndex, sampleCounts, 3)) {
        postProcessing.AmbientOcclusion->SampleCount = static_cast<uint32_t>(8 << sampleCountIndex);
        changed = true;
      }
      changed |=
          ImGui::SliderFloat("Depth bias", &postProcessing.AmbientOcclusion->DepthBias, 0.0F, 0.1F, "%.3f scene units");
      ImGui::Unindent();
    }
    std::optional<std::pair<size_t, size_t>> reorderEffect;
    std::optional<size_t> removeEffect;
    if (!postProcessing.Effects.empty()) {
      ImGui::TextDisabled("Drag an effect header to change its position in the render order.");
    }
    for (size_t index = 0; index < postProcessing.Effects.size(); ++index) {
      ImGui::PushID(static_cast<int>(index));
      Engine::Ecs::CameraPostProcessEffect& effect = postProcessing.Effects[index];
      const char* effectName = std::holds_alternative<Engine::Ecs::ColorGradingEffect>(effect) ? "Color Grading"
                               : std::holds_alternative<Engine::Ecs::BloomEffect>(effect)      ? "Bloom"
                                                                                               : "FXAA";
      const bool effectOpen =
          ImGui::TreeNodeEx("Effect", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%zu. %s",
                            index + 1U, effectName);
      if (ImGui::BeginDragDropSource()) {
        const int sourceIndex = static_cast<int>(index);
        ImGui::SetDragDropPayload(kCameraPostProcessEffectPayload, &sourceIndex, sizeof(sourceIndex));
        ImGui::Text("Move %s", effectName);
        ImGui::EndDragDropSource();
      }
      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kCameraPostProcessEffectPayload);
            payload != nullptr && payload->DataSize == static_cast<int>(sizeof(int))) {
          const int sourceIndex = *static_cast<const int*>(payload->Data);
          if (sourceIndex >= 0 && sourceIndex < static_cast<int>(postProcessing.Effects.size()) &&
              sourceIndex != static_cast<int>(index)) {
            reorderEffect = std::pair{static_cast<size_t>(sourceIndex), index};
          }
        }
        ImGui::EndDragDropTarget();
      }
      if (effectOpen) {
        if (auto* grading = std::get_if<Engine::Ecs::ColorGradingEffect>(&effect)) {
          const float exposureMin = std::min(-5.0F, grading->Exposure);
          const float exposureMax = std::max(5.0F, grading->Exposure);
          const float contrastMax = std::max(2.0F, grading->Contrast);
          const float saturationMax = std::max(2.0F, grading->Saturation);
          changed |= ImGui::SliderFloat("Exposure", &grading->Exposure, exposureMin, exposureMax, "%.2f stops");
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Exposure multiplier is 2^stops. Typical adjustments fit within -5 to +5 stops.");
          }
          changed |= ImGui::SliderFloat("Contrast", &grading->Contrast, 0.0F, contrastMax, "%.2f");
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Contrast around 18%% linear gray. 1 is neutral; 0 to 2 is the practical range.");
          }
          changed |= ImGui::SliderFloat("Saturation", &grading->Saturation, 0.0F, saturationMax, "%.3f");
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Saturation multiplier. 1 is neutral; 0 to 2 is the practical range.");
          }
        } else if (auto* bloom = std::get_if<Engine::Ecs::BloomEffect>(&effect)) {
          changed |= ImGui::DragFloat("Threshold", &bloom->Threshold, 0.05F, 0.0F, 100000.0F, "%.2f");
          changed |= ImGui::SliderFloat("Intensity", &bloom->Intensity, 0.0F, 10.0F, "%.2f");
          changed |= ImGui::SliderFloat("Radius", &bloom->Radius, 0.0F, 64.0F, "%.1f px");
          changed |= ImGui::Checkbox("Smooth bloom", &bloom->Smooth);
        } else if (auto* fxaa = std::get_if<Engine::Ecs::FxaaEffect>(&effect)) {
          static constexpr const char* qualityNames[] = {"Low", "Medium", "High"};
          int qualityIndex = static_cast<int>(fxaa->Quality);
          if (ImGui::Combo("Quality", &qualityIndex, qualityNames, 3)) {
            fxaa->Quality = static_cast<Engine::Ecs::FxaaQuality>(qualityIndex);
            changed = true;
          }
          changed |= ImGui::SliderFloat("Edge threshold", &fxaa->EdgeThreshold, 0.01F, 0.5F, "%.3f");
        } else {
          ImGui::TextDisabled("No parameters.");
        }

        ImGui::BeginDisabled(index == 0U);
        if (ImGui::SmallButton("Up")) {
          reorderEffect = std::pair{index, index - 1U};
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(index + 1U == postProcessing.Effects.size());
        if (ImGui::SmallButton("Down")) {
          reorderEffect = std::pair{index, index + 1U};
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove effect")) {
          removeEffect = index;
        }
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
    if (removeEffect) {
      postProcessing.Effects.erase(postProcessing.Effects.begin() + static_cast<std::ptrdiff_t>(*removeEffect));
      changed = true;
    } else if (reorderEffect) {
      auto effect = std::move(postProcessing.Effects[reorderEffect->first]);
      postProcessing.Effects.erase(postProcessing.Effects.begin() + static_cast<std::ptrdiff_t>(reorderEffect->first));
      postProcessing.Effects.insert(postProcessing.Effects.begin() + static_cast<std::ptrdiff_t>(reorderEffect->second),
                                    std::move(effect));
      changed = true;
    }
    static constexpr const char* effectTypes[] = {"Color Grading", "Bloom", "FXAA"};
    static int newEffectType = 0;
    ImGui::SetNextItemWidth(150.0F);
    ImGui::Combo("New effect", &newEffectType, effectTypes, 3);
    ImGui::SameLine();
    if (ImGui::Button("Add effect")) {
      if (newEffectType == 0) {
        postProcessing.Effects.emplace_back(Engine::Ecs::ColorGradingEffect{});
      } else if (newEffectType == 1) {
        postProcessing.Effects.emplace_back(Engine::Ecs::BloomEffect{});
      } else {
        postProcessing.Effects.emplace_back(Engine::Ecs::FxaaEffect{});
      }
      changed = true;
    }
    if (changed) {
      emitSet(Mutation::Component::CameraPostProcessing, std::move(postProcessing));
    }
  }

  if (!isSceneRoot && ImGui::Button("Add Component")) {
    m_AddComponentFilter.fill('\0');
    ImGui::OpenPopup("Add component popup");
  }
  if (!isSceneRoot && ImGui::BeginPopup("Add component popup")) {
    (void)ImGui::InputTextWithHint("##add-filter", "Search components...", m_AddComponentFilter.data(),
                                   m_AddComponentFilter.size());
    if (!world.Has<Engine::Ecs::MeshRenderer>(entity) && Matches("Mesh", m_AddComponentFilter.data()) &&
        ImGui::Selectable("Mesh")) {
      emitAdd(Mutation::Component::MeshRenderer);
    }
    if (!world.Has<Engine::Ecs::SpotLight>(entity) && Matches("Light", m_AddComponentFilter.data()) &&
        ImGui::Selectable("Light")) {
      emitAdd(Mutation::Component::SpotLight);
    }
    if (!world.Has<Engine::Ecs::Camera>(entity) && Matches("Camera", m_AddComponentFilter.data()) &&
        ImGui::Selectable("Camera")) {
      emitAdd(Mutation::Component::Camera);
    }
    if (world.Has<Engine::Ecs::Camera>(entity) && !world.Has<Engine::Ecs::CameraPostProcessing>(entity) &&
        Matches("Post Processing", m_AddComponentFilter.data()) && ImGui::Selectable("Post Processing")) {
      emitAdd(Mutation::Component::CameraPostProcessing);
    }
    ImGui::EndPopup();
  }

  std::string signature = "{";
  const auto append = [&signature](const char* component) -> void {
    if (signature.size() > 1) {
      signature += ", ";
    }
    signature += component;
  };
  append("PersistentIdentity");
  if (world.Has<Engine::Ecs::Name>(entity)) {
    append("Name");
  }
  if (world.Has<Engine::Ecs::LocalTransform>(entity)) {
    append("Transform");
  }
  if (world.Has<Engine::Ecs::Parent>(entity) && world.Has<Engine::Ecs::ChildList>(entity)) {
    append("Parent / ChildList");
  }
  if (world.Has<Engine::Ecs::MeshRenderer>(entity)) {
    append("MeshRenderer");
  }
  if (world.Has<Engine::Ecs::SpotLight>(entity)) {
    append("SpotLight");
  }
  if (world.Has<Engine::Ecs::Camera>(entity)) {
    append("Camera");
  }
  if (world.Has<Engine::Ecs::CameraPostProcessing>(entity)) {
    append("CameraPostProcessing");
  }
  if (world.Has<Engine::Ecs::Skybox>(entity)) {
    append("Skybox");
  }
  signature += '}';
  ImGui::Separator();
  ImGui::BeginDisabled();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  ImGui::TextWrapped("Archetype: %s", signature.c_str());
  ImGui::EndDisabled();
}

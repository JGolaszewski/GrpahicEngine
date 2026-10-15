#include "panels/include/AssetBrowserPanel.h"

#include "events/include/AssetImportRequestedEvent.h"
#include "events/include/AssetRegistryChangedEvent.h"
#include "events/include/AssetReimportRequestedEvent.h"
#include "events/include/ModelReassignmentRequestedEvent.h"
#include <algorithm>
#include <imgui.h>

void AssetBrowserPanel::Draw() {
  ImGui::Begin("Assets", nullptr, ImGuiWindowFlags_NoCollapse);
  if (ImGui::Button("Import model...")) {
    EmitEvent(std::make_unique<AssetImportRequestedEvent>());
  }
  ImGui::SameLine();
  if (ImGui::Button("Import texture...")) {
    EmitEvent(std::make_unique<AssetImportRequestedEvent>(AssetImportRequestedEvent::AssetType::Texture));
  }
  ImGui::SameLine();
  if (ImGui::Button("Reimport selected") && !m_SelectedGuid.empty()) {
    EmitEvent(std::make_unique<AssetReimportRequestedEvent>(m_SelectedGuid));
  }
  const bool selectedModel =
      std::ranges::any_of(m_Models, [this](const auto& model) -> auto { return model.guid == m_SelectedGuid; });
  ImGui::SameLine();
  if (!selectedModel) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Replace selected object model") && selectedModel) {
    EmitEvent(std::make_unique<ModelReassignmentRequestedEvent>(m_SelectedGuid));
  }
  if (!selectedModel) {
    ImGui::EndDisabled();
  }
  ImGui::Separator();
  if (m_Models.empty() && m_Textures.empty()) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::TextDisabled("No assets found in this project.");
  }
  if (!m_Models.empty()) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::TextDisabled("Models");
  }
  for (const auto& model : m_Models) {
    ImGui::PushID(model.guid.c_str());
    const bool selected = m_SelectedGuid == model.guid;
    if (ImGui::Selectable(model.name.c_str(), selected)) {
      m_SelectedGuid = model.guid;
    }
    if (ImGui::IsItemHovered()) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
      ImGui::SetTooltip("%s\n%s", model.guid.c_str(), model.sourcePath.generic_string().c_str());
    }
    ImGui::PopID();
  }
  if (!m_Textures.empty()) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::TextDisabled("Textures");
  }
  for (const auto& texture : m_Textures) {
    ImGui::PushID(texture.guid.c_str());
    const bool selected = m_SelectedGuid == texture.guid;
    const std::string label = texture.name + "  [Texture]";
    if (ImGui::Selectable(label.c_str(), selected)) {
      m_SelectedGuid = texture.guid;
    }
    if (ImGui::IsItemHovered()) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
      ImGui::SetTooltip("Texture\n%s\n%s", texture.guid.c_str(), texture.sourcePath.generic_string().c_str());
    }
    ImGui::PopID();
  }
  ImGui::End();
}

void AssetBrowserPanel::OnEvent(Engine::Events::Event& event) {
  Engine::Events::EventDispatcher dispatcher(event);
  dispatcher.Dispatch<AssetRegistryChangedEvent>([this](const auto& update) -> auto {
    m_Models = update.GetModels();
    m_Textures = update.GetTextures();
    if (std::none_of(m_Models.begin(), m_Models.end(),
                     [this](const auto& model) -> auto { return model.guid == m_SelectedGuid; })) {
      if (std::none_of(m_Textures.begin(), m_Textures.end(),
                       [this](const auto& texture) -> auto { return texture.guid == m_SelectedGuid; })) {
        m_SelectedGuid.clear();
      }
    }
    return false;
  });
}

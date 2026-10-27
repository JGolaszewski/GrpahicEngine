#include "panels/include/AssetBrowserPanel.h"

#include "events/include/AssetImportRequestedEvent.h"
#include "events/include/AssetRegistryChangedEvent.h"
#include "events/include/AssetReimportRequestedEvent.h"
#include "events/include/AssetRenameRequestedEvent.h"
#include "events/include/AssetWorkflowFeedbackEvent.h"
#include "events/include/ModelReassignmentRequestedEvent.h"
#include "panels/include/AssetDragDropPayload.h"
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
  if (ImGui::Button("Import HDR environment...")) {
    EmitEvent(std::make_unique<AssetImportRequestedEvent>(AssetImportRequestedEvent::AssetType::Environment));
  }
  ImGui::SameLine();
  if (ImGui::Button("Create material")) {
    EmitEvent(std::make_unique<AssetImportRequestedEvent>(AssetImportRequestedEvent::AssetType::Material));
  }
  ImGui::SameLine();
  const bool selectedModel =
      std::ranges::any_of(m_Models, [this](const auto& model) -> auto { return model.guid == m_SelectedGuid; });
  const auto selectedTexture =
      std::ranges::find(m_Textures, m_SelectedGuid, &Engine::AssetManager::TextureAssetInfo::guid);
  const auto selectedEnvironment =
      std::ranges::find(m_Environments, m_SelectedGuid, &Engine::AssetManager::EnvironmentAssetInfo::guid);
  const bool selectedImportableAsset = selectedModel || selectedTexture != m_Textures.end();
  if (!selectedImportableAsset) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Reimport selected") && selectedImportableAsset) {
    EmitEvent(std::make_unique<AssetReimportRequestedEvent>(m_SelectedGuid));
  }
  if (!selectedImportableAsset) {
    ImGui::EndDisabled();
  }
  ImGui::SameLine();
  if (!selectedModel) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Assign model") && selectedModel) {
    EmitEvent(std::make_unique<ModelReassignmentRequestedEvent>(m_SelectedGuid));
  }
  if (!selectedModel) {
    ImGui::EndDisabled();
  }
  const std::string* selectedName =
      selectedModel ? &std::ranges::find(m_Models, m_SelectedGuid, &Engine::AssetManager::ModelAssetInfo::guid)->name
      : selectedTexture != m_Textures.end()         ? &selectedTexture->name
      : selectedEnvironment != m_Environments.end() ? &selectedEnvironment->name
      : std::ranges::find(m_Materials, m_SelectedGuid, &Engine::AssetManager::MaterialAssetInfo::guid) !=
              m_Materials.end()
          ? &std::ranges::find(m_Materials, m_SelectedGuid, &Engine::AssetManager::MaterialAssetInfo::guid)->name
          : nullptr;
  ImGui::SameLine();
  if (selectedName == nullptr) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Rename selected") && selectedName != nullptr) {
    m_RenameBuffer.fill('\0');
    const size_t length = (std::min)(selectedName->size(), m_RenameBuffer.size() - 1);
    std::copy_n(selectedName->data(), length, m_RenameBuffer.data());
    ImGui::OpenPopup("Rename asset");
  }
  if (selectedName == nullptr) {
    ImGui::EndDisabled();
  }
  if (ImGui::BeginPopupModal("Rename asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    (void)ImGui::InputText("New name", m_RenameBuffer.data(), m_RenameBuffer.size());
    if (ImGui::Button("Rename") && m_RenameBuffer[0] != '\0') {
      EmitEvent(std::make_unique<AssetRenameRequestedEvent>(m_SelectedGuid, m_RenameBuffer.data()));
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::Separator();
  if (!m_StatusMessage.empty()) {
    ImGui::TextColored(m_StatusSucceeded ? ImVec4(0.45F, 0.85F, 0.5F, 1.0F) : ImVec4(0.95F, 0.4F, 0.35F, 1.0F), "%s",
                       m_StatusMessage.c_str());
  }
  if (m_Models.empty() && m_Textures.empty() && m_Materials.empty() && m_Environments.empty()) {
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
    if (ImGui::BeginDragDropSource()) {
      if (const auto payload = MakeAssetDragDropPayload(model.guid)) {
        ImGui::SetDragDropPayload(ModelAssetDragDropType, &*payload, sizeof(*payload));
        ImGui::Text("Model: %s", model.name.c_str());
      }
      ImGui::EndDragDropSource();
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
    if (ImGui::BeginDragDropSource()) {
      if (const auto payload = MakeAssetDragDropPayload(texture.guid)) {
        ImGui::SetDragDropPayload(TextureAssetDragDropType, &*payload, sizeof(*payload));
        ImGui::Text("Texture: %s", texture.name.c_str());
      }
      ImGui::EndDragDropSource();
    }
    if (ImGui::IsItemHovered()) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
      ImGui::SetTooltip("Texture\n%s\n%s", texture.guid.c_str(), texture.sourcePath.generic_string().c_str());
    }
    ImGui::PopID();
  }
  if (!m_Environments.empty()) {
    ImGui::TextDisabled("HDR Environments");
  }
  for (const auto& environment : m_Environments) {
    ImGui::PushID(environment.guid.c_str());
    const std::string label = environment.name + "  [HDR Environment]";
    if (ImGui::Selectable(label.c_str(), m_SelectedGuid == environment.guid)) {
      m_SelectedGuid = environment.guid;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("HDR Environment\n%s\n%s", environment.guid.c_str(),
                        environment.sourcePath.generic_string().c_str());
    }
    ImGui::PopID();
  }
  if (!m_Materials.empty()) {
    ImGui::TextDisabled("Materials");
  }
  for (const auto& material : m_Materials) {
    ImGui::PushID(material.guid.c_str());
    const std::string label = material.name + "  [Material]";
    if (ImGui::Selectable(label.c_str(), m_SelectedGuid == material.guid)) {
      m_SelectedGuid = material.guid;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Material\n%s\n%s", material.guid.c_str(), material.sourcePath.generic_string().c_str());
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
    m_Materials = update.GetMaterials();
    m_Environments = update.GetEnvironments();
    if (std::none_of(m_Models.begin(), m_Models.end(),
                     [this](const auto& model) -> auto { return model.guid == m_SelectedGuid; })) {
      if (std::none_of(m_Textures.begin(), m_Textures.end(),
                       [this](const auto& texture) -> auto { return texture.guid == m_SelectedGuid; })) {
        if (std::none_of(m_Materials.begin(), m_Materials.end(),
                         [this](const auto& material) -> auto { return material.guid == m_SelectedGuid; })) {
          if (std::none_of(m_Environments.begin(), m_Environments.end(),
                           [this](const auto& environment) -> auto { return environment.guid == m_SelectedGuid; })) {
            m_SelectedGuid.clear();
          }
        }
      }
    }
    return false;
  });
  dispatcher.Dispatch<AssetWorkflowFeedbackEvent>([this](const auto& update) -> bool {
    m_StatusSucceeded = update.Succeeded();
    m_StatusMessage = update.GetMessage();
    return false;
  });
}

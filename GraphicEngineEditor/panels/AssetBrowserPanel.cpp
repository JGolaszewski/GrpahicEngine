#include "AssetBrowserPanel.h"

#include "events/AssetImportRequestedEvent.h"
#include "events/AssetRegistryChangedEvent.h"
#include <imgui.h>
#include <algorithm>

void AssetBrowserPanel::Draw() {
	ImGui::Begin("Assets", nullptr, ImGuiWindowFlags_NoCollapse);
	if (ImGui::Button("Import model...")) EmitEvent(std::make_unique<AssetImportRequestedEvent>());
	ImGui::SameLine();
	if (ImGui::Button("Import texture...")) EmitEvent(std::make_unique<AssetImportRequestedEvent>(AssetImportRequestedEvent::AssetType::Texture));
	ImGui::Separator();
	if (m_Models.empty() && m_Textures.empty()) ImGui::TextDisabled("No assets found in this project.");
	if (!m_Models.empty()) ImGui::TextDisabled("Models");
	for (const auto& model : m_Models) {
		const bool selected = m_SelectedGuid == model.guid;
		if (ImGui::Selectable(model.name.c_str(), selected)) m_SelectedGuid = model.guid;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", model.guid.c_str(), model.sourcePath.generic_string().c_str());
	}
	if (!m_Textures.empty()) ImGui::TextDisabled("Textures");
	for (const auto& texture : m_Textures) {
		const bool selected = m_SelectedGuid == texture.guid;
		const std::string label = texture.name + "  [Texture]";
		if (ImGui::Selectable(label.c_str(), selected)) m_SelectedGuid = texture.guid;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Texture\n%s\n%s", texture.guid.c_str(), texture.sourcePath.generic_string().c_str());
	}
	ImGui::End();
}

void AssetBrowserPanel::OnEvent(Engine::Events::Event& event) {
	Engine::Events::EventDispatcher dispatcher(event);
	dispatcher.Dispatch<AssetRegistryChangedEvent>([this](const auto& update) {
		m_Models = update.GetModels();
		m_Textures = update.GetTextures();
		if (std::none_of(m_Models.begin(), m_Models.end(), [this](const auto& model) { return model.guid == m_SelectedGuid; })) {
			if (std::none_of(m_Textures.begin(), m_Textures.end(), [this](const auto& texture) { return texture.guid == m_SelectedGuid; }))
				m_SelectedGuid.clear();
		}
		return false;
	});
}

#include "AssetBrowserPanel.h"

#include "events/AssetImportRequestedEvent.h"
#include "events/AssetRegistryChangedEvent.h"
#include <imgui.h>
#include <algorithm>

void AssetBrowserPanel::Draw() {
	ImGui::Begin("Assets", nullptr, ImGuiWindowFlags_NoCollapse);
	if (ImGui::Button("Import model...")) {
		EmitEvent(std::make_unique<AssetImportRequestedEvent>());
	}
	ImGui::Separator();
	if (m_Models.empty()) {
		ImGui::TextDisabled("No model assets imported in this session.");
	}
	for (const auto& model : m_Models) {
		const bool selected = m_SelectedGuid == model.guid;
		if (ImGui::Selectable(model.name.c_str(), selected)) m_SelectedGuid = model.guid;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", model.guid.c_str(), model.sourcePath.generic_string().c_str());
	}
	ImGui::End();
}

void AssetBrowserPanel::OnEvent(Engine::Events::Event& event) {
	Engine::Events::EventDispatcher dispatcher(event);
	dispatcher.Dispatch<AssetRegistryChangedEvent>([this](const auto& update) {
		m_Models = update.GetModels();
		if (std::none_of(m_Models.begin(), m_Models.end(), [this](const auto& model) { return model.guid == m_SelectedGuid; })) {
			m_SelectedGuid.clear();
		}
		return false;
	});
}

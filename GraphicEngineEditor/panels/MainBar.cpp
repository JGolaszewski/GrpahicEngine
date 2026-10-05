#include "MainBar.h"

#include <imgui.h>
#include "events/SceneFileActionRequestedEvent.h"

void MainBar::Draw() {
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("New", "Ctrl+N")) EmitEvent(std::make_unique<SceneFileActionRequestedEvent>(SceneFileActionRequestedEvent::Action::NewScene));
			if (ImGui::MenuItem("Open...", "Ctrl+O")) EmitEvent(std::make_unique<SceneFileActionRequestedEvent>(SceneFileActionRequestedEvent::Action::OpenScene));
			ImGui::Separator();
			if (ImGui::MenuItem("Save", "Ctrl+S")) EmitEvent(std::make_unique<SceneFileActionRequestedEvent>(SceneFileActionRequestedEvent::Action::SaveScene));
			if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) EmitEvent(std::make_unique<SceneFileActionRequestedEvent>(SceneFileActionRequestedEvent::Action::SaveSceneAs));
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}
}

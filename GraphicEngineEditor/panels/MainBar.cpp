#include "MainBar.h"

#include <imgui.h>

void MainBar::Draw() {
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Close", "Alt+F4")) {
				// TODO: Close window via application event
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}
}
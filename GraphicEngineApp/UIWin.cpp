#include "UIWin.h"



void statWindow() {
	ImGui::Begin("Statystyki");

	float fps = ImGui::GetIO().Framerate;
	float frameTime = 1000.0f / fps;

	ImGui::Text("FPS: %.1f", fps);
	ImGui::Text("Czas klatki: %.3f ms", frameTime);

	static float fpsHistory[100] = { 0.0f };
	static int historyIdx = 0;

	fpsHistory[historyIdx] = fps;
	historyIdx = (historyIdx + 1) % 100;

	ImGui::Spacing();

	ImGui::PlotLines("##WykresFPS", fpsHistory, 100, historyIdx, "Historia FPS", 0.0f, 200.0f, ImVec2(0, 80));

	static float timeHistory[100] = { 0.0f };
	timeHistory[historyIdx] = frameTime;
	ImGui::PlotLines("##WykresCzasu", timeHistory, 100, historyIdx, "Frametime (ms)", 0.0f, 33.3f, ImVec2(0, 50));

	ImGui::End();
}

void cameraWindow(Components::Transform* transform) {
	if(!transform) return;

	ImGui::Begin("Sterowanie Kamer¹");

	float pos[3] = { transform->Position.x, transform->Position.y, transform->Position.z };
	if(ImGui::InputFloat3("Pozycja", pos)) {
		transform->SetPosition(pos[0], pos[1], pos[2]);
	}

	static bool lookAtCenter = false;
	ImGui::Checkbox("Look at center (0,0,0)", &lookAtCenter);

	if(lookAtCenter) {
		transform->LookAt(0.0f, 0.0f, 0.0f);

		ImGui::TextDisabled("Rotacja (Zablokowana przez LookAt)");
		ImGui::Text("Quat: %.2f, %.2f, %.2f, %.2f",
			transform->Rotation.x, transform->Rotation.y,
			transform->Rotation.z, transform->Rotation.w);
	} else {
		static float euler[3] = { 0.0f, 0.0f, 0.0f };

		if(ImGui::SliderFloat3("Rotacja (Pitch/Yaw/Roll)", euler, -3.14f, 3.14f)) {
			transform->SetRotation(euler[0], euler[1], euler[2]);
		}
	}

	if(ImGui::Button("Resetuj Kamerê")) {
		transform->SetPosition(0.0f, 0.0f, -5.0f);
		transform->SetRotation(0.0f, 0.0f, 0.0f);
		lookAtCenter = false;
	}

	ImGui::End();
}

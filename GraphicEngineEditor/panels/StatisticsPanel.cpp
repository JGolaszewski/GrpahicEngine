#include "StatisticsPanel.h"

#include <imgui.h>

void StatWindow::Draw() {
	ImGui::Begin("Statistics", nullptr);

	const float fps = ImGui::GetIO().Framerate;
	const float frameTime = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;

	ImGui::Text("FPS: %.1f", fps);
	ImGui::Text("Frametime: %.3f ms", frameTime);

	m_HistoryIdx = (m_HistoryIdx + 1) % 100;

	ImGui::Spacing();

	ImGui::PlotLines("##FPSChart", m_FpsHistory, 100, m_HistoryIdx, "FPS History", 0.0f, 200.0f, ImVec2(0, 80));

	m_TimeHistory[m_HistoryIdx] = frameTime;
	ImGui::PlotLines("##TimeChart", m_TimeHistory, 100, m_HistoryIdx, "Frametime (ms)", 0.0f, 33.3f, ImVec2(0, 50));

	ImGui::End();
}
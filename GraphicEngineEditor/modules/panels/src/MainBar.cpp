#include "panels/include/MainBar.h"

#include "events/include/SceneFileActionRequestedEvent.h"
#include <imgui.h>

void MainBar::Draw() {
  if (ImGui::BeginMainMenuBar()) {
    DrawFileMenu();
    DrawWindowMenu();
    ImGui::EndMainMenuBar();
  }
}

void MainBar::DrawFileMenu() {
  if (!ImGui::BeginMenu("File")) {
    return;
  }
  if (ImGui::MenuItem("Open Project Folder...")) {
    EmitEvent(std::make_unique<SceneFileActionRequestedEvent>(SceneFileActionRequestedEvent::Action::OpenProject));
  }
  if (ImGui::MenuItem("Reset Sandbox")) {
    EmitEvent(std::make_unique<SceneFileActionRequestedEvent>(SceneFileActionRequestedEvent::Action::ResetSandbox));
  }
  ImGui::Separator();
  if (ImGui::MenuItem("New", "Ctrl+N")) {
    EmitEvent(std::make_unique<SceneFileActionRequestedEvent>(SceneFileActionRequestedEvent::Action::NewScene));
  }
  if (ImGui::MenuItem("Open...", "Ctrl+O")) {
    EmitEvent(std::make_unique<SceneFileActionRequestedEvent>(SceneFileActionRequestedEvent::Action::OpenScene));
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Save", "Ctrl+S")) {
    EmitEvent(std::make_unique<SceneFileActionRequestedEvent>(SceneFileActionRequestedEvent::Action::SaveScene));
  }
  if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
    EmitEvent(std::make_unique<SceneFileActionRequestedEvent>(SceneFileActionRequestedEvent::Action::SaveSceneAs));
  }
  ImGui::EndMenu();
}

void MainBar::DrawWindowMenu() {
  if (!ImGui::BeginMenu("Window")) {
    return;
  }
  if (ImGui::MenuItem("Settings") && m_SettingsOpenHandler) {
    m_SettingsOpenHandler();
  }
  if (ImGui::MenuItem("Console") && m_ConsoleOpenHandler) {
    m_ConsoleOpenHandler();
  }
  if (m_AnalyticalFrameWorkspaceHandler) {
    const char* workspaceMenuItem =
        m_AnalyticalFrameWorkspaceActive ? "Exit Analytical Frame Workspace" : "Analytical Frame Workspace";
    if (ImGui::MenuItem(workspaceMenuItem)) {
      m_AnalyticalFrameWorkspaceHandler(!m_AnalyticalFrameWorkspaceActive);
    }
  }
  ImGui::EndMenu();
}

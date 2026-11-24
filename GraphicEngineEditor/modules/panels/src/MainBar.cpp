#include "panels/include/MainBar.h"

#include "events/include/CommandHistoryActionRequestedEvent.h"
#include "events/include/SceneFileActionRequestedEvent.h"
#include <imgui.h>

void MainBar::Draw() {
  if (ImGui::BeginMainMenuBar()) {
    DrawFileMenu();
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo")) {
        EmitEvent(
            std::make_unique<CommandHistoryActionRequestedEvent>(CommandHistoryActionRequestedEvent::Action::Undo));
      }
      if (ImGui::MenuItem("Redo")) {
        EmitEvent(
            std::make_unique<CommandHistoryActionRequestedEvent>(CommandHistoryActionRequestedEvent::Action::Redo));
      }
      ImGui::EndMenu();
    }
    DrawViewMenu();
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

void MainBar::DrawViewMenu() {
  if (!ImGui::BeginMenu("View")) {
    return;
  }
  for (const PanelMenuItem& item : m_PanelMenuItems) {
    const bool isOpen = item.IsOpen && item.IsOpen();
    if (ImGui::MenuItem(item.Label.c_str(), nullptr, isOpen) && item.SetOpen) {
      item.SetOpen(!isOpen);
    }
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Reset Layout") && m_ResetLayoutHandler) {
    m_ResetLayoutHandler();
  }
  if (ImGui::MenuItem("Save Layout") && m_SaveLayoutHandler) {
    m_SaveLayoutHandler();
  }
  if (ImGui::MenuItem("Load Layout") && m_LoadLayoutHandler) {
    m_LoadLayoutHandler();
  }
  ImGui::EndMenu();
}

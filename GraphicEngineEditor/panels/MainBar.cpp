#include "MainBar.h"

#include "events/SceneFileActionRequestedEvent.h"
#include <imgui.h>

void MainBar::Draw()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open Project Folder..."))
                EmitEvent(std::make_unique<SceneFileActionRequestedEvent>(
                    SceneFileActionRequestedEvent::Action::OpenProject));
            ImGui::Separator();
            if (ImGui::MenuItem("New", "Ctrl+N"))
                EmitEvent(std::make_unique<SceneFileActionRequestedEvent>(
                    SceneFileActionRequestedEvent::Action::NewScene));
            if (ImGui::MenuItem("Open...", "Ctrl+O"))
                EmitEvent(std::make_unique<SceneFileActionRequestedEvent>(
                    SceneFileActionRequestedEvent::Action::OpenScene));
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S"))
                EmitEvent(std::make_unique<SceneFileActionRequestedEvent>(
                    SceneFileActionRequestedEvent::Action::SaveScene));
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
                EmitEvent(std::make_unique<SceneFileActionRequestedEvent>(
                    SceneFileActionRequestedEvent::Action::SaveSceneAs));
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window"))
        {
            if (ImGui::MenuItem("Engine Console") && m_ConsoleOpenHandler)
                m_ConsoleOpenHandler();
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

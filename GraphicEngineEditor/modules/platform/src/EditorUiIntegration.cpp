#include "platform/include/EditorUiIntegration.h"

#include "rendering/include/RenderBackend.h"
#include "rendering/include/ViewportTarget.h"
#include "runtime/include/Window.h"

#include <algorithm>
#include <imgui.h>

bool EditorUiIntegration::Initialize(Engine::IWindow *window, Engine::IRenderBackend *backend)
{
    if (!window || !backend)
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

    if (!backend->InitializeEditorUi(window))
    {
        ImGui::DestroyContext();
        return false;
    }

    m_Backend = backend;
    m_Initialized = true;
    return true;
}

void EditorUiIntegration::BeginFrame()
{
    if (!m_Initialized)
        return;
    m_Backend->BeginEditorUiFrame();
    ImGui::NewFrame();
}

void EditorUiIntegration::EndFrame()
{
    if (!m_Initialized || !m_Backend)
        return;
    ImGui::Render();
    m_Backend->RenderEditorUi(ImGui::GetDrawData());
}

void EditorUiIntegration::Shutdown()
{
    if (!m_Initialized)
        return;
    m_Backend->ShutdownEditorUi();
    ImGui::DestroyContext();
    m_Backend = nullptr;
    m_Initialized = false;
}

void *EditorUiIntegration::GetTextureId(const Engine::ViewportTarget *target) const noexcept
{
    if (!m_Initialized || !target)
        return nullptr;
    return m_Backend->RegisterViewportTexture(*target);
}

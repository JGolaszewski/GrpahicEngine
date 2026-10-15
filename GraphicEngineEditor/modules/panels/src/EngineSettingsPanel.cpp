#include "panels/include/EngineSettingsPanel.h"

#include "runtime/include/Application.h"
#include "rendering/include/RenderBackend.h"
#include "rendering/include/RenderSystem.h"
#include "rendering/include/RenderLimits.h"

#include <algorithm>
#include <imgui.h>

void EngineSettingsPanel::Draw()
{
    if (!m_Open)
        return;

    if (!ImGui::Begin("Engine Settings", &m_Open))
    {
        ImGui::End();
        return;
    }

    if (!m_Application)
    {
        ImGui::TextDisabled("Application settings are unavailable.");
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Runtime");
    bool paused = m_Application->IsSimulationPaused();
    if (ImGui::Checkbox("Pause simulation", &paused))
        m_Application->SetSimulationPaused(paused);

    int frameLimit = static_cast<int>(m_Application->GetFrameLimiterHz());
    if (ImGui::SliderInt("Frame limit (Hz)", &frameLimit, 0, 240, frameLimit == 0 ? "Off" : "%d Hz"))
        m_Application->SetFrameLimiterHz(static_cast<uint32_t>((std::max)(frameLimit, 0)));
    ImGui::SetItemTooltip("0 disables the engine frame limiter. VSync may still limit presentation.");

    ImGui::SeparatorText("Rendering");
    Engine::RenderSystem *renderSystem = m_Application->GetRenderSystem();
    if (renderSystem)
    {
        Engine::ShadowSettings shadowSettings = renderSystem->GetShadowSettings();
        int shadowResolution = static_cast<int>(shadowSettings.Resolution);
        if (ImGui::DragInt("Shadow map resolution", &shadowResolution, 64.0f, 128, 8192, "%d px"))
        {
            shadowSettings.Resolution = static_cast<uint32_t>((std::clamp)(shadowResolution, 128, 8192));
            renderSystem->SetShadowSettings(shadowSettings);
        }
        ImGui::SetItemTooltip(
            "Higher resolutions improve shadow detail and use more GPU memory and bandwidth.");

        int shadowCasters = static_cast<int>(shadowSettings.MaxShadowCastersPerScene);
        if (ImGui::SliderInt("Shadow-casting lights", &shadowCasters, 0,
                             static_cast<int>(Engine::MaxShadowedLights)))
        {
            shadowSettings.MaxShadowCastersPerScene = static_cast<uint32_t>(shadowCasters);
            renderSystem->SetShadowSettings(shadowSettings);
        }
        ImGui::SetItemTooltip(
            "Maximum shadow maps rendered per scene. The current renderer supports up to %u.",
            Engine::MaxShadowedLights);
    }
    else
    {
        ImGui::TextDisabled("Renderer settings are unavailable.");
    }

    ImGui::SeparatorText("Backend");
    Engine::IRenderBackend *backend = m_Application->GetRenderer();
    if (backend)
    {
        Engine::BackendDiagnostics diagnostics = backend->GetDiagnostics();
        ImGui::Text("Active backend: %s", diagnostics.BackendName);
        ImGui::Text("Device: %s", diagnostics.DeviceReady ? "Ready" : "Unavailable");
        ImGui::Text("Adapter vendor/device: 0x%04X / 0x%04X", diagnostics.AdapterVendorId,
                    diagnostics.AdapterDeviceId);
        if (backend->SupportsVSyncControl())
        {
            bool vsync = diagnostics.VSyncEnabled;
            if (ImGui::Checkbox("VSync", &vsync))
                backend->SetVSyncEnabled(vsync);
            ImGui::SetItemTooltip(
                "Applied on the next present. This can cap the frame rate to the display refresh rate.");
        }
        else
        {
            ImGui::TextDisabled("VSync control is not supported by this backend.");
        }
        ImGui::TextDisabled("Backend and adapter selection require an editor restart.");
    }
    else
    {
        ImGui::TextDisabled("No render backend is available.");
    }

    ImGui::End();
}

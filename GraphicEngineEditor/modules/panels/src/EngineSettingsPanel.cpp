#include "panels/include/EngineSettingsPanel.h"

#include "rendering/include/RenderBackend.h"
#include "rendering/include/RenderSystem.h"
#include "runtime/include/Application.h"

#include <algorithm>
#include <imgui.h>

void EngineSettingsPanel::Draw() {
  if (!m_Open) {
    return;
  }

  if (!ImGui::Begin("Engine Settings", &m_Open)) {
    ImGui::End();
    return;
  }

  if (m_Application == nullptr) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::TextDisabled("Application settings are unavailable.");
    ImGui::End();
    return;
  }

  ImGui::SeparatorText("Runtime");
  bool paused = m_Application->IsSimulationPaused();
  if (ImGui::Checkbox("Pause simulation", &paused)) {
    m_Application->SetSimulationPaused(paused);
  }

  int frameLimit = static_cast<int>(m_Application->GetFrameLimiterHz());
  if (ImGui::SliderInt("Frame limit (Hz)", &frameLimit, 0, 240, frameLimit == 0 ? "Off" : "%d Hz")) {
    m_Application->SetFrameLimiterHz(static_cast<uint32_t>((std::max)(frameLimit, 0)));
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::SetItemTooltip("0 disables the engine frame limiter. VSync may still limit presentation.");

  ImGui::SeparatorText("Rendering");
  Engine::RenderSystem* renderSystem = m_Application->GetRenderSystem();
  if (renderSystem != nullptr) {
    Engine::ShadowSettings shadowSettings = renderSystem->GetShadowSettings();
    int shadowResolution = static_cast<int>(shadowSettings.Resolution);
    if (ImGui::DragInt("Shadow map resolution", &shadowResolution, 64.0F, 128, 8192, "%d px")) {
      shadowSettings.Resolution = static_cast<uint32_t>(std::clamp(shadowResolution, 128, 8192));
      renderSystem->SetShadowSettings(shadowSettings);
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::SetItemTooltip("Higher resolutions improve shadow detail and use more GPU memory and bandwidth.");

    int shadowCasters = static_cast<int>(shadowSettings.MaxShadowCastersPerScene);
    if (ImGui::SliderInt("Shadow-casting lights", &shadowCasters, 0,
                         static_cast<int>(Engine::SceneRenderer::MaxShadowedLights))) {
      shadowSettings.MaxShadowCastersPerScene = static_cast<uint32_t>(shadowCasters);
      renderSystem->SetShadowSettings(shadowSettings);
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::SetItemTooltip("Maximum shadow maps rendered per scene. The current renderer supports up to %u.",
                          Engine::SceneRenderer::MaxShadowedLights);
  } else {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::TextDisabled("Renderer settings are unavailable.");
  }

  ImGui::SeparatorText("Backend");
  Engine::IRenderBackend* backend = m_Application->GetRenderer();
  if (backend != nullptr) {
    Engine::BackendDiagnostics diagnostics = backend->GetDiagnostics();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::Text("Active backend: %s", diagnostics.BackendName);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::Text("Device: %s", diagnostics.DeviceReady ? "Ready" : "Unavailable");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::Text("Adapter vendor/device: 0x%04X / 0x%04X", diagnostics.AdapterVendorId, diagnostics.AdapterDeviceId);
    if (backend->SupportsVSyncControl()) {
      bool vsync = diagnostics.VSyncEnabled;
      if (ImGui::Checkbox("VSync", &vsync)) {
        backend->SetVSyncEnabled(vsync);
      }
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
      ImGui::SetItemTooltip("Applied on the next present. This can cap the frame rate to the display refresh rate.");
    } else {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
      ImGui::TextDisabled("VSync control is not supported by this backend.");
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::TextDisabled("Backend and adapter selection require an editor restart.");
  } else {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::TextDisabled("No render backend is available.");
  }

  ImGui::End();
}

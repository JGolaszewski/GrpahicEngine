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

  if (!ImGui::Begin("Settings", &m_Open)) {
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
    if (m_GetPreferences && m_SetPreferences) {
      auto preferences = m_GetPreferences();
      preferences.FrameLimiterHz = static_cast<uint32_t>((std::max)(frameLimit, 0));
      m_SetPreferences(preferences);
    }
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
  ImGui::SetItemTooltip("0 disables the engine frame limiter. VSync may still limit presentation.");

  ImGui::SeparatorText("Editor Rendering");
  Engine::RenderSystem* renderSystem = m_Application->GetRenderSystem();
  if (m_GetPreferences && m_SetPreferences) {
    auto preferences = m_GetPreferences();
    bool changed = ImGui::Checkbox("Show light gizmos", &preferences.ShowLightGizmos);
    changed |= ImGui::Checkbox("Show camera frustums", &preferences.ShowCameraFrustums);
    changed |= ImGui::Checkbox("Show wireframe plane", &preferences.ShowWireframePlane);
    constexpr int anisotropyValues[] = {1, 2, 4, 8, 16};
    constexpr const char* anisotropyLabels[] = {"1x", "2x", "4x", "8x", "16x"};
    int anisotropyIndex = 3;
    for (int index = 0; index < 5; ++index) {
      if (preferences.MaxTextureAnisotropy == static_cast<std::uint32_t>(anisotropyValues[index])) {
        anisotropyIndex = index;
        break;
      }
    }
    if (renderSystem != nullptr && ImGui::Combo("Maximum texture anisotropy", &anisotropyIndex, anisotropyLabels, 5)) {
      const auto maximum = static_cast<std::uint32_t>(anisotropyValues[anisotropyIndex]);
      if (renderSystem->SetMaxTextureAnisotropy(maximum)) {
        preferences.MaxTextureAnisotropy = maximum;
        changed = true;
      }
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::SetItemTooltip("Caps anisotropic filtering requests made by material texture slots.");
    ImGui::SeparatorText("Preview");
    float displayScale = preferences.CameraPreviewScale * 100.0F;
    float renderQuality = preferences.CameraPreviewQuality * 100.0F;
    if (ImGui::SliderFloat("Camera PiP display scale", &displayScale, 10.0F, 80.0F, "%.0f%%")) {
      preferences.CameraPreviewScale = displayScale / 100.0F;
      changed = true;
    }
    if (ImGui::SliderFloat("Camera PiP render quality", &renderQuality, 5.0F, 100.0F, "%.0f%%")) {
      preferences.CameraPreviewQuality = renderQuality / 100.0F;
      changed = true;
    }
    if (changed) {
      m_SetPreferences(preferences);
    }
  }
  if (renderSystem != nullptr) {
    ImGui::SeparatorText("Shadows");
    Engine::ShadowSettings shadowSettings = renderSystem->GetShadowSettings();
    int shadowResolution = static_cast<int>(shadowSettings.Resolution);
    if (ImGui::DragInt("Shadow map resolution", &shadowResolution, 64.0F, 128, 8192, "%d px")) {
      shadowSettings.Resolution = static_cast<uint32_t>(std::clamp(shadowResolution, 128, 8192));
      if (renderSystem->SetShadowSettings(shadowSettings) && m_GetPreferences && m_SetPreferences) {
        auto preferences = m_GetPreferences();
        preferences.ShadowMapResolution = shadowSettings.Resolution;
        m_SetPreferences(preferences);
      }
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::SetItemTooltip("Higher resolutions improve shadow detail and use more GPU memory and bandwidth.");

    int shadowCasters = static_cast<int>(shadowSettings.MaxShadowCastersPerScene);
    if (ImGui::SliderInt("Shadow-casting lights", &shadowCasters, 0, static_cast<int>(Engine::MaxShadowedLights))) {
      shadowSettings.MaxShadowCastersPerScene = static_cast<uint32_t>(shadowCasters);
      if (renderSystem->SetShadowSettings(shadowSettings) && m_GetPreferences && m_SetPreferences) {
        auto preferences = m_GetPreferences();
        preferences.MaxShadowCasters = shadowSettings.MaxShadowCastersPerScene;
        m_SetPreferences(preferences);
      }
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::SetItemTooltip("Maximum shadow maps rendered per scene. The current renderer supports up to %u.",
                          Engine::MaxShadowedLights);

    if (ImGui::TreeNode("Directional shadow fit diagnostics")) {
      const auto& diagnostics = renderSystem->GetDirectionalShadowDiagnostics();
      if (diagnostics.empty()) {
        ImGui::TextDisabled("No directional shadow fit is active in the latest frame.");
      }
      for (const auto& fit : diagnostics) {
        ImGui::PushID(static_cast<int>(fit.Light.Index));
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // ImGui formats diagnostic numbers through its documented
        // variadic API.
        ImGui::Text("Light %u", fit.Light.Index);
        ImGui::Text("Coverage: %.2f x %.2f x %.2f world units", fit.CoverageWidth, fit.CoverageWidth,
                    fit.CoverageDepth);
        ImGui::Text("Map: %u x %u px (%.4f world units / texel)", fit.MapResolution, fit.MapResolution,
                    fit.TexelWorldSize);
        ImGui::Text("Depth bias: %.5f", fit.Bias);
        ImGui::PopID();
      }
      ImGui::TreePop();
    }
  } else {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) // Dear ImGui formats text through its documented variadic API.
    ImGui::TextDisabled("Renderer settings are unavailable.");
  }

  ImGui::SeparatorText("Graphics Backend");
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
        if (backend->SetVSyncEnabled(vsync) && m_GetPreferences && m_SetPreferences) {
          auto preferences = m_GetPreferences();
          preferences.VSyncEnabled = vsync;
          m_SetPreferences(preferences);
        }
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

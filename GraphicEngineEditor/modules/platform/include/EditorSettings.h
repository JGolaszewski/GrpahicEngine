#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace EditorPlatform {
/// User-wide editor preferences that are saved between editor launches.
struct EditorPreferences {
  std::uint32_t FrameLimiterHz = 0;
  bool ShowLightGizmos = true;
  bool ShowCameraFrustums = true;
  bool ShowWireframePlane = true;
  bool ShowSkyboxInEditorViewport = true; ///< Whether the scene skybox background is shown in the editor viewport.
  float CameraPreviewScale = 0.35F;
  float CameraPreviewQuality = 0.25F;
  std::uint32_t ShadowMapResolution = 2048;
  std::uint32_t MaxShadowCasters = 4;
  std::uint32_t MaxTextureAnisotropy = 8;
  bool VSyncEnabled = true;
};

/// Result from loading a user-wide settings file. Missing files use defaults without an error.
struct EditorSettingsLoadResult {
  EditorPreferences Preferences;
  bool Found = false;
  std::string Error;
};

/// Loads settings JSON from a specific path, returning defaults when it is missing or invalid.
/// @param path Settings file path.
/// @return Loaded settings, whether a file existed, and a diagnostic on read/parse/validation failure.
[[nodiscard]] EditorSettingsLoadResult LoadEditorSettingsFile(const std::filesystem::path& path);

/// Saves user-wide editor preferences as categorized JSON.
/// @param path Settings file path.
/// @param preferences Preferences to persist.
/// @param error Receives a useful diagnostic when saving fails.
/// @return True when the complete file was written.
[[nodiscard]] bool SaveEditorSettingsFile(const std::filesystem::path& path, const EditorPreferences& preferences,
                                          std::string& error);
} // namespace EditorPlatform

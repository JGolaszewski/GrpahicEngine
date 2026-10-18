#include "platform/include/EditorSettings.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <system_error>

namespace EditorPlatform {
namespace {
using Json = nlohmann::json;

auto ReadBoolean(const Json& object, const char* key, bool fallback) -> bool {
  if (!object.contains(key)) {
    return fallback;
  }
  if (!object.at(key).is_boolean()) {
    throw std::runtime_error(std::string("expected a boolean for '") + key + "'");
  }
  return object.at(key).get<bool>();
}

auto ReadPercentage(const Json& object, const char* key, float fallback, float minimum, float maximum) -> float {
  if (!object.contains(key)) {
    return fallback;
  }
  const Json& value = object.at(key);
  if (!value.is_number()) {
    throw std::runtime_error(std::string("expected a number for '") + key + "'");
  }
  const float parsed = value.get<float>();
  if (!std::isfinite(parsed)) {
    throw std::runtime_error(std::string("expected a finite number for '") + key + "'");
  }
  return std::clamp(parsed, minimum, maximum);
}

auto ReadUnsigned(const Json& object, const char* key, std::uint32_t fallback, std::uint32_t minimum,
                  std::uint32_t maximum) -> std::uint32_t {
  if (!object.contains(key)) {
    return fallback;
  }
  const Json& value = object.at(key);
  if (!value.is_number_unsigned() && !value.is_number_integer()) {
    throw std::runtime_error(std::string("expected a non-negative integer for '") + key + "'");
  }
  const int64_t parsed = value.get<int64_t>();
  if (parsed < 0) {
    throw std::runtime_error(std::string("expected a non-negative integer for '") + key + "'");
  }
  const auto bounded = std::clamp(parsed, static_cast<std::int64_t>(minimum), static_cast<std::int64_t>(maximum));
  return static_cast<std::uint32_t>(bounded);
}
} // namespace

auto LoadEditorSettingsFile(const std::filesystem::path& path) -> EditorSettingsLoadResult {
  EditorSettingsLoadResult result;
  std::error_code filesystemError;
  const bool exists = std::filesystem::exists(path, filesystemError);
  if (!exists && !filesystemError) {
    return result;
  }
  result.Found = true;
  if (filesystemError) {
    result.Error = "could not inspect settings file: " + filesystemError.message();
    return result;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    result.Error = "could not open settings file for reading";
    return result;
  }

  try {
    const Json document = Json::parse(input);
    if (!document.is_object() || !document.contains("version") || !document.at("version").is_number_integer() ||
        document.at("version").get<int>() != 1) {
      throw std::runtime_error("expected a settings object with supported version 1");
    }
    if (!document.contains("runtime") || !document.at("runtime").is_object() || !document.contains("rendering") ||
        !document.at("rendering").is_object() || !document.contains("camera_preview") ||
        !document.at("camera_preview").is_object()) {
      throw std::runtime_error("expected 'runtime', 'rendering', and 'camera_preview' settings categories");
    }

    const Json& runtime = document.at("runtime");
    const Json& rendering = document.at("rendering");
    const Json& preview = document.at("camera_preview");
    result.Preferences.FrameLimiterHz = ReadUnsigned(runtime, "frame_limiter_hz", 0, 0, 240);
    result.Preferences.ShowLightGizmos = ReadBoolean(rendering, "show_light_gizmos", true);
    result.Preferences.ShowCameraFrustums = ReadBoolean(rendering, "show_camera_frustums", true);
    result.Preferences.ShowWireframePlane = ReadBoolean(rendering, "show_wireframe_plane", true);
    result.Preferences.ShadowMapResolution = ReadUnsigned(rendering, "shadow_map_resolution", 2048, 128, 8192);
    result.Preferences.MaxShadowCasters = ReadUnsigned(rendering, "max_shadow_casters", 4, 0, 4);
    result.Preferences.VSyncEnabled = ReadBoolean(rendering, "vsync_enabled", true);
    result.Preferences.CameraPreviewScale = ReadPercentage(preview, "display_scale", 0.35F, 0.1F, 0.8F);
    result.Preferences.CameraPreviewQuality = ReadPercentage(preview, "render_quality", 0.25F, 0.05F, 1.0F);
  } catch (const std::exception& exception) {
    result.Preferences = {};
    result.Error = std::string("invalid settings JSON: ") + exception.what();
  }
  return result;
}

auto SaveEditorSettingsFile(const std::filesystem::path& path, const EditorPreferences& preferences, std::string& error)
    -> bool {
  error.clear();
  if (!std::isfinite(preferences.CameraPreviewScale) || !std::isfinite(preferences.CameraPreviewQuality)) {
    error = "camera preview settings must be finite numbers";
    return false;
  }
  std::error_code filesystemError;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), filesystemError);
  }
  if (filesystemError) {
    error = "could not create settings directory: " + filesystemError.message();
    return false;
  }

  const Json document = {{"version", 1},
                         {"runtime", {{"frame_limiter_hz", std::min(preferences.FrameLimiterHz, 240U)}}},
                         {"rendering",
                          {{"show_light_gizmos", preferences.ShowLightGizmos},
                           {"show_camera_frustums", preferences.ShowCameraFrustums},
                           {"show_wireframe_plane", preferences.ShowWireframePlane},
                           {"shadow_map_resolution", std::clamp(preferences.ShadowMapResolution, 128U, 8192U)},
                           {"max_shadow_casters", std::min(preferences.MaxShadowCasters, 4U)},
                           {"vsync_enabled", preferences.VSyncEnabled}}},
                         {"camera_preview",
                          {{"display_scale", std::clamp(preferences.CameraPreviewScale, 0.1F, 0.8F)},
                           {"render_quality", std::clamp(preferences.CameraPreviewQuality, 0.05F, 1.0F)}}}};
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "could not open settings file for writing";
    return false;
  }
  output << document.dump(2) << '\n';
  if (!output) {
    error = "could not write settings JSON";
    return false;
  }
  return true;
}
} // namespace EditorPlatform

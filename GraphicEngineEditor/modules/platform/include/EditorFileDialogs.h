#pragma once

#include <filesystem>
#include <optional>

namespace EditorPlatform {
struct EditorPreferences {
  bool ShowLightGizmos = true;
  bool ShowCameraFrustums = true;
  bool ShowWireframePlane = true;
  float CameraPreviewScale = 0.35F;
  float CameraPreviewQuality = 0.25F;
};
EditorPreferences LoadEditorPreferences();
void SaveEditorPreferences(const EditorPreferences& preferences);
std::optional<std::filesystem::path> ChooseSceneFile(bool save, const std::filesystem::path& initialPath,
                                                     const std::filesystem::path& projectRoot);
std::optional<std::filesystem::path> ChooseModelFile(const std::filesystem::path& projectRoot);
std::optional<std::filesystem::path> ChooseTextureFile(const std::filesystem::path& projectRoot);
std::optional<std::filesystem::path> ChooseLogExportFile(const std::filesystem::path& initialPath);
/// Opens an overwrite-confirming save dialog for a final-render image.
/// @param exr When true, selects the multilayer OpenEXR filter; otherwise PNG.
std::optional<std::filesystem::path> ChooseRenderOutputFile(bool exr, const std::filesystem::path& initialPath);
bool OpenSourceFile(const std::filesystem::path& path);
std::optional<std::filesystem::path> ChooseProjectFolder(const std::filesystem::path& initialPath);
std::filesystem::path LoadRememberedProjectRoot();
void RememberProjectRoot(const std::filesystem::path& root);
} // namespace EditorPlatform

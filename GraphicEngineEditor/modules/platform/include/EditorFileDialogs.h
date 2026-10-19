#pragma once

#include "platform/include/EditorSettings.h"

#include <filesystem>
#include <optional>

namespace EditorPlatform {
EditorPreferences LoadEditorPreferences();
void SaveEditorPreferences(const EditorPreferences& preferences);
std::optional<std::filesystem::path> ChooseSceneFile(bool save, const std::filesystem::path& initialPath,
                                                     const std::filesystem::path& projectRoot);
std::optional<std::filesystem::path> ChooseModelFile(const std::filesystem::path& projectRoot);
std::optional<std::filesystem::path> ChooseTextureFile(const std::filesystem::path& projectRoot);
std::optional<std::filesystem::path> ChooseLogExportFile(const std::filesystem::path& initialPath);
/// Opens an overwrite-confirming save dialog for a final-render image.
/// @param exr When true, selects the multilayer OpenEXR filter; otherwise PNG.
/// @param initialPath Initial selection and filename shown by the dialog.
std::optional<std::filesystem::path> ChooseRenderOutputFile(bool exr, const std::filesystem::path& initialPath);
/// Opens an EXR or Snapshot Analysis workspace save/open dialog.
/// @param exr Selects an OpenEXR payload; false selects a versioned .aframe workspace.
/// @param save Opens a save dialog when true and an existing-file dialog when false.
/// @param initialPath Initial directory and filename shown by the dialog.
std::optional<std::filesystem::path> ChooseSnapshotFile(bool exr, bool save, const std::filesystem::path& initialPath);
bool OpenSourceFile(const std::filesystem::path& path);
std::optional<std::filesystem::path> ChooseProjectFolder(const std::filesystem::path& initialPath);
std::filesystem::path LoadRememberedProjectRoot();
void RememberProjectRoot(const std::filesystem::path& root);
} // namespace EditorPlatform

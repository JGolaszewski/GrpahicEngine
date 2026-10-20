#pragma once

#include "platform/include/EditorSettings.h"

#include <cstdint>
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
/// Opens an overwrite-confirming save dialog for a BRDF analysis artifact.
/// @param json When true, selects the JSON measurements filter; otherwise PNG chart.
/// @param initialPath Initial selection and filename shown by the dialog.
std::optional<std::filesystem::path> ChooseBrdfAnalysisFile(bool json, const std::filesystem::path& initialPath);
/// File operation used to choose an Analytical Frame archive or export destination.
enum class AnalyticalFrameFileOperation : uint8_t { OpenArchive, SaveArchive, ExportChart, ExportMeasurements };
/// Opens or saves a self-contained Analytical Frame project, chart, or measurement file.
/// @param operation Archive or export operation.
/// @param initialPath Initial selection and filename shown by the dialog.
std::optional<std::filesystem::path> ChooseAnalyticalFrameFile(AnalyticalFrameFileOperation operation,
                                                               const std::filesystem::path& initialPath);
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

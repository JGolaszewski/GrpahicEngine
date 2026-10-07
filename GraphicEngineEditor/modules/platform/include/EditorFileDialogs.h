#pragma once

#include <filesystem>
#include <optional>

namespace EditorPlatform
{
std::optional<std::filesystem::path> ChooseSceneFile(bool save, const std::filesystem::path &initialPath,
                                                     const std::filesystem::path &projectRoot);
std::optional<std::filesystem::path> ChooseModelFile(const std::filesystem::path &projectRoot);
std::optional<std::filesystem::path> ChooseTextureFile(const std::filesystem::path &projectRoot);
std::optional<std::filesystem::path> ChooseLogExportFile(const std::filesystem::path &initialPath);
bool OpenSourceFile(const std::filesystem::path &path);
std::optional<std::filesystem::path> ChooseProjectFolder(const std::filesystem::path &initialPath);
std::filesystem::path LoadRememberedProjectRoot();
void RememberProjectRoot(const std::filesystem::path &root);
} // namespace EditorPlatform

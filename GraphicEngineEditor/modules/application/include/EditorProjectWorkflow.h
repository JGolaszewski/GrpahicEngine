#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Engine {
class AssetManager;
}

namespace EditorWorkflow {
enum class ProjectOpenResult { Cancelled, Failed, Opened };

/// Applies a project-folder selection and restores the prior project if registry loading fails.
/// @param assets Asset manager whose project root is being changed.
/// @param path Selected project folder, or empty when the user cancelled.
/// @param warnings Receives non-fatal registry warnings.
/// @param error Receives the fatal selection or registry diagnostic.
/// @return Whether the selection was cancelled, failed, or opened.
ProjectOpenResult OpenProject(Engine::AssetManager& assets, const std::optional<std::filesystem::path>& path,
                              std::vector<std::string>& warnings, std::string& error);
} // namespace EditorWorkflow

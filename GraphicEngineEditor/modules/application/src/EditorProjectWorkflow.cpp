#include "application/include/EditorProjectWorkflow.h"

#include "assets/include/AssetManager.h"

namespace EditorWorkflow {
auto OpenProject(Engine::AssetManager& assets, const std::optional<std::filesystem::path>& path,
                 std::vector<std::string>& warnings, std::string& error) -> ProjectOpenResult {
  warnings.clear();
  error.clear();
  if (!path) {
    return ProjectOpenResult::Cancelled;
  }

  const std::filesystem::path previousRoot = assets.GetProjectRoot();
  if (!assets.SetProjectRoot(*path, error)) {
    return ProjectOpenResult::Failed;
  }
  if (assets.RefreshRegistry(warnings, error)) {
    return ProjectOpenResult::Opened;
  }

  const std::string selectionError = error;
  std::vector<std::string> restoreWarnings;
  std::string restoreError;
  if (assets.SetProjectRoot(previousRoot, restoreError) && assets.RefreshRegistry(restoreWarnings, restoreError)) {
    error = selectionError;
    return ProjectOpenResult::Failed;
  }

  error = selectionError + "; restoring the previous project also failed: " + restoreError;
  return ProjectOpenResult::Failed;
}
} // namespace EditorWorkflow

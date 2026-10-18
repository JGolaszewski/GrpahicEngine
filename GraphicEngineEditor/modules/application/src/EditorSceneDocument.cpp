#include "application/include/EditorSceneDocument.h"

#include "assets/include/AssetManager.h"
#include "scene/include/SceneDocument.h"

#include <system_error>

auto EditorSceneDocument::Canonicalize(const std::filesystem::path& path) -> std::filesystem::path {
  std::error_code error;
  auto canonical = std::filesystem::weakly_canonical(path, error);
  return error ? path.lexically_normal() : canonical;
}

auto EditorSceneDocument::Load(const Engine::AssetManager& assets, const std::filesystem::path& path)
    -> Engine::SceneDocumentResult {
  return assets.LoadScene(path);
}

auto EditorSceneDocument::Open(
    const std::optional<std::filesystem::path>& path,
    const std::function<bool(const std::filesystem::path&, std::string&, std::vector<std::string>&)>& activate,
    std::string& error, std::vector<std::string>& warnings) -> OpenResult {
  error.clear();
  warnings.clear();
  if (!path) {
    return OpenResult::Cancelled;
  }
  if (!activate(*path, error, warnings)) {
    return OpenResult::Failed;
  }
  CommitPath(*path);
  return OpenResult::Opened;
}

auto EditorSceneDocument::Save(const Engine::AssetManager& assets, const Engine::SceneDocument& document,
                               const std::filesystem::path& path) -> Engine::Status {
  std::string error;
  if (!assets.SaveScene(path, document, error)) {
    return std::unexpected(Engine::MakeError(std::move(error)));
  }
  m_Path = Canonicalize(path);
  return {};
}

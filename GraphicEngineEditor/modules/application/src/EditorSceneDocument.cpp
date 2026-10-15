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

auto EditorSceneDocument::Save(const Engine::AssetManager& assets, const Engine::SceneDocument& document,
                               const std::filesystem::path& path) -> Engine::Status {
  std::string error;
  if (!assets.SaveScene(path, document, error)) {
    return std::unexpected(Engine::MakeError(std::move(error)));
  }
  m_Path = Canonicalize(path);
  return {};
}

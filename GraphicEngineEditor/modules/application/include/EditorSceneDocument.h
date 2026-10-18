#pragma once

#include "runtime/include/Result.h"
#include "scene/include/SceneDocument.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Engine {
class AssetManager;
struct SceneDocument;
} // namespace Engine

class EditorSceneDocument {
public:
  enum class OpenResult { Cancelled, Failed, Opened };

  const std::filesystem::path& GetPath() const noexcept { return m_Path; }
  void Reset() noexcept { m_Path.clear(); }
  void CommitPath(const std::filesystem::path& path) { m_Path = Canonicalize(path); }
  /// Opens a selected scene and commits its path only after the activation callback succeeds.
  /// @param path Selected path, or empty when the user cancelled the file dialog.
  /// @param activate Callback that loads and activates the selected scene.
  /// @param error Receives the loading diagnostic on failure.
  /// @param warnings Receives non-fatal scene loading warnings.
  /// @return Whether the selection was cancelled, failed, or activated.
  OpenResult
  Open(const std::optional<std::filesystem::path>& path,
       const std::function<bool(const std::filesystem::path&, std::string&, std::vector<std::string>&)>& activate,
       std::string& error, std::vector<std::string>& warnings);
  static Engine::SceneDocumentResult Load(const Engine::AssetManager& assets, const std::filesystem::path& path);
  Engine::Status Save(const Engine::AssetManager& assets, const Engine::SceneDocument& document,
                      const std::filesystem::path& path);

private:
  std::filesystem::path m_Path;
  static std::filesystem::path Canonicalize(const std::filesystem::path& path);
};

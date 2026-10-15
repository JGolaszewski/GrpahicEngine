#pragma once

#include "assets/include/AssetImporter.h"
#include "runtime/include/CoreExport.h"
#include "runtime/include/Result.h"
#include "scene/include/SceneDocument.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine {
/// Imports and tracks project assets and reads or writes scene documents.
///
/// Imported assets are addressed by registry GUIDs. Configure the project root
/// before using operations that resolve paths relative to the project.
class ENGINE_API AssetManager {
public:
  /// Metadata for a model known to the asset registry.
  struct ModelAssetInfo {
    std::string guid;
    std::string name;
    std::filesystem::path sourcePath;
  };
  /// Metadata for a texture known to the asset registry.
  struct TextureAssetInfo {
    std::string guid;
    std::string name;
    std::filesystem::path sourcePath;
  };
  AssetManager() = default;
  /// Construct a manager rooted at the given project directory.
  explicit AssetManager(const std::filesystem::path& projectRoot);

  /// Set the project root, returning false and an error message if it is invalid.
  bool SetProjectRoot(const std::filesystem::path& projectRoot, std::string& error);
  const std::filesystem::path& GetProjectRoot() const noexcept { return m_ProjectRoot; }

  /// Import a model and return its registry GUID, or an error on failure.
  Result<std::string> ImportModel(const std::filesystem::path& sourcePath);
  /// Import a texture and return its registry GUID, or an error on failure.
  Result<std::string> ImportTexture(const std::filesystem::path& sourcePath);
  bool ImportModel(const std::filesystem::path& sourcePath, std::string& guid, std::string& error);
  bool ImportTexture(const std::filesystem::path& sourcePath, std::string& guid, std::string& error);
  bool LoadModel(const std::string& guid, std::string& error);
  bool LoadTexture(const std::string& guid, std::string& error);
  /// Refresh registry entries from disk and report non-fatal issues as warnings.
  bool RefreshRegistry(std::vector<std::string>& warnings, std::string& error);
  /// Return a loaded model, or an empty pointer when the GUID is not loaded.
  std::shared_ptr<const ImportedModel> GetModel(const std::string& guid) const;
  /// Return a loaded texture, or an empty pointer when the GUID is not loaded.
  std::shared_ptr<const ImportedImage> GetTexture(const std::string& guid) const;
  std::optional<uint64_t> GetGeneration(const std::string& guid) const;
  std::optional<std::filesystem::path> GetSourcePath(const std::string& guid) const;
  std::vector<ModelAssetInfo> GetKnownModels() const;
  std::vector<TextureAssetInfo> GetKnownTextures() const;
  static std::string GetEmbeddedImageIdentity(const std::string& modelGuid, std::size_t imageIndex);

  /// Load a scene document from a path interpreted relative to the project root.
  SceneDocumentResult LoadScene(const std::filesystem::path& path) const;
  /// Save a scene document and return false with a diagnostic on failure.
  bool SaveScene(const std::filesystem::path& path, const SceneDocument& document, std::string& error) const;

private:
  struct ModelRecord {
    std::filesystem::path sourcePath;
    std::shared_ptr<const ImportedModel> model;
    uint64_t generation = 1;
  };
  struct TextureRecord {
    std::filesystem::path sourcePath;
    std::shared_ptr<const ImportedImage> image;
    uint64_t generation = 1;
  };

  std::filesystem::path m_ProjectRoot;
  std::unordered_map<std::string, ModelRecord> m_Models;
  std::unordered_map<std::string, TextureRecord> m_Textures;
  AssetImporter m_Importer;
  SceneSerializer m_SceneSerializer;
};
} // namespace Engine

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

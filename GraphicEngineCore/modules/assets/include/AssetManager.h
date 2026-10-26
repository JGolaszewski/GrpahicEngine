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
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine {
/// Imports, caches, and persists project assets under a configured project root.
class ENGINE_API AssetManager {
public:
  /// Registry information for a known model asset.
  struct ModelAssetInfo {
    std::string guid;                 ///< Stable identifier used by scene documents.
    std::string name;                 ///< Display name derived from the source model.
    std::filesystem::path sourcePath; ///< Resolved source file path.
  };

  /// Registry information for a known texture asset.
  struct TextureAssetInfo {
    std::string guid;                 ///< Stable identifier used by material documents.
    std::string name;                 ///< Display name derived from the source image.
    std::filesystem::path sourcePath; ///< Resolved source file path.
  };

  /// Registry information for a Radiance HDR environment asset.
  struct EnvironmentAssetInfo {
    std::string guid;                 ///< Stable identifier used by scene skybox components.
    std::string name;                 ///< Display name derived from the source image.
    std::filesystem::path sourcePath; ///< Project-relative source file path.
  };

  /// Registry information for a reusable material asset.
  struct MaterialAssetInfo {
    std::string guid;                 ///< Stable identifier used by scene documents.
    std::string name;                 ///< Display name derived from the asset filename.
    std::filesystem::path sourcePath; ///< Project-relative material document path.
  };

  /// Creates a manager without a project root; call SetProjectRoot before importing.
  AssetManager() = default;

  /// Creates a manager and configures its project root.
  /// @param projectRoot Directory containing the project asset registry.
  explicit AssetManager(const std::filesystem::path& projectRoot);

  /// Changes the project root and loads or initializes its asset registry.
  /// @param projectRoot New project directory.
  /// @param error Receives a diagnostic when the root or registry is invalid.
  /// @return True when the root is usable.
  bool SetProjectRoot(const std::filesystem::path& projectRoot, std::string& error);

  /// Returns the configured project root.
  /// @return The project root used for registry and asset paths.
  const std::filesystem::path& GetProjectRoot() const noexcept { return m_ProjectRoot; }

  /// Imports a model and returns its stable asset identifier.
  /// @param sourcePath Model file to import.
  /// @return Asset identifier on success, otherwise an error message.
  Result<std::string> ImportModel(const std::filesystem::path& sourcePath);

  /// Imports an image and returns its stable asset identifier.
  /// @param sourcePath Image file to import.
  /// @return Asset identifier on success, otherwise an error message.
  Result<std::string> ImportTexture(const std::filesystem::path& sourcePath);

  /// Imports a Radiance HDR environment and returns its stable asset identifier.
  /// @param sourcePath Equirectangular .hdr file inside the project root.
  /// @return Asset identifier on success, otherwise an error message.
  Result<std::string> ImportEnvironment(const std::filesystem::path& sourcePath);

  /// Creates a reusable material asset initialized with the supplied properties.
  /// @param name Display name and filename stem for the new material.
  /// @param material Material properties to persist.
  /// @return Asset identifier on success, otherwise an error message.
  Result<std::string> CreateMaterial(std::string_view name, const SceneMaterialDocument& material = {});

  /// Loads and caches the material document associated with an asset identifier.
  /// @param guid Registered material identifier.
  /// @param error Receives a diagnostic when lookup or parsing fails.
  /// @return True when the material is available in the cache.
  bool LoadMaterial(const std::string& guid, std::string& error);

  /// Replaces the persisted properties of a registered material without changing its identity.
  /// @param guid Stable material identifier.
  /// @param material New properties to persist.
  /// @param error Receives a diagnostic when lookup or saving fails.
  /// @return True when the asset document and cache are updated.
  bool SaveMaterial(const std::string& guid, const SceneMaterialDocument& material, std::string& error);

  /// Imports a model using output parameters for the identifier and diagnostic.
  /// @param sourcePath Model file to import.
  /// @param guid Receives the registered model identifier.
  /// @param error Receives the import or registry diagnostic.
  /// @return True when import and registry update succeed.
  bool ImportModel(const std::filesystem::path& sourcePath, std::string& guid, std::string& error);

  /// Imports an image using output parameters for the identifier and diagnostic.
  /// @param sourcePath Image file to import.
  /// @param guid Receives the registered texture identifier.
  /// @param error Receives the import or registry diagnostic.
  /// @return True when import and registry update succeed.
  bool ImportTexture(const std::filesystem::path& sourcePath, std::string& guid, std::string& error);

  /// Imports a Radiance HDR environment using output parameters for the identifier and diagnostic.
  /// @param sourcePath Equirectangular .hdr file inside the project root.
  /// @param guid Receives the registered environment identifier.
  /// @param error Receives the import or registry diagnostic.
  /// @return True when import and registry update succeed.
  bool ImportEnvironment(const std::filesystem::path& sourcePath, std::string& guid, std::string& error);

  /// Renames a registered model, texture, HDR environment, or material while retaining its persistent asset identifier.
  /// @param guid Stable identifier of the registered asset.
  /// @param newName New filename without an extension.
  /// @param error Receives a diagnostic when validation or either file rename fails.
  /// @return True when the source file and adjacent metadata sidecar are renamed.
  bool RenameAsset(const std::string& guid, const std::string& newName, std::string& error);

  /// Loads and caches the decoded model associated with an asset identifier.
  /// @param guid Registered model identifier.
  /// @param error Receives a diagnostic when lookup or decoding fails.
  /// @return True when the model is available in the cache.
  bool LoadModel(const std::string& guid, std::string& error);

  /// Loads and caches the decoded image associated with an asset identifier.
  /// @param guid Registered texture identifier.
  /// @param error Receives a diagnostic when lookup or decoding fails.
  /// @return True when the image is available in the cache.
  bool LoadTexture(const std::string& guid, std::string& error);

  /// Loads and caches the decoded environment associated with an identifier.
  /// @param guid Registered environment identifier.
  /// @param error Receives a diagnostic when lookup or decoding fails.
  /// @return True when the environment is available in the cache.
  bool LoadEnvironment(const std::string& guid, std::string& error);

  /// Re-scans and validates the persisted registry.
  /// @param warnings Receives recoverable registry issues.
  /// @param error Receives a fatal registry error.
  /// @return True when the registry is valid and refreshed.
  bool RefreshRegistry(std::vector<std::string>& warnings, std::string& error);

  /// Returns the cached model, or null when it has not been loaded.
  /// @param guid Registered model identifier.
  /// @return The cached model, or an empty pointer when it is not loaded.
  std::shared_ptr<const ImportedModel> GetModel(const std::string& guid) const;

  /// Returns the cached image, or null when it has not been loaded.
  /// @param guid Registered texture identifier.
  /// @return The cached image, or an empty pointer when it is not loaded.
  std::shared_ptr<const ImportedImage> GetTexture(const std::string& guid) const;

  /// Returns the cached HDR environment, or null when it has not been loaded.
  /// @param guid Registered environment identifier.
  /// @return The cached environment, or an empty pointer when it is not loaded.
  std::shared_ptr<const ImportedEnvironment> GetEnvironment(const std::string& guid) const;

  /// Returns the cache generation for a loaded asset.
  /// @param guid Registered asset identifier.
  /// @return Generation value, or empty when the asset is unknown or unloaded.
  std::optional<uint64_t> GetGeneration(const std::string& guid) const;

  /// Returns the registered source path for an asset.
  /// @param guid Registered asset identifier.
  /// @return Source path, or empty when the identifier is unknown.
  std::optional<std::filesystem::path> GetSourcePath(const std::string& guid) const;

  /// Lists the known model assets in the registry.
  /// @return A snapshot of the registered model assets.
  std::vector<ModelAssetInfo> GetKnownModels() const;

  /// Lists the known texture assets in the registry.
  /// @return A snapshot of the registered texture assets.
  std::vector<TextureAssetInfo> GetKnownTextures() const;

  /// Lists the known HDR environment assets in the registry.
  /// @return A snapshot of registered environments.
  std::vector<EnvironmentAssetInfo> GetKnownEnvironments() const;

  /// Lists the known reusable material assets.
  /// @return A snapshot of the registered material assets.
  std::vector<MaterialAssetInfo> GetKnownMaterials() const;

  /// Returns the cached material document, or empty when it has not been loaded.
  /// @param guid Registered material identifier.
  /// @return The material document, or empty when it is not loaded.
  std::optional<SceneMaterialDocument> GetMaterial(const std::string& guid) const;

  /// Creates the stable identity for an image embedded in a model file.
  /// @param modelGuid Identifier of the parent model asset.
  /// @param imageIndex Zero-based image index within the imported model.
  /// @return Deterministic identifier for the embedded image.
  static std::string GetEmbeddedImageIdentity(const std::string& modelGuid, std::size_t imageIndex);

  /// Loads a scene document from disk through the scene serializer.
  /// @param path Scene file path.
  /// @return Parsed document, compatibility warnings, or a load error.
  SceneDocumentResult LoadScene(const std::filesystem::path& path) const;

  /// Atomically saves a scene document to disk.
  /// @param path Destination scene file path.
  /// @param document Scene data to save.
  /// @param error Receives a diagnostic when saving fails.
  /// @return True when the document is saved successfully.
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
  struct EnvironmentRecord {
    std::filesystem::path sourcePath;
    std::shared_ptr<const ImportedEnvironment> environment;
    uint64_t generation = 1;
  };
  struct MaterialRecord {
    std::filesystem::path sourcePath;
    SceneMaterialDocument material;
  };

  std::filesystem::path m_ProjectRoot;
  std::unordered_map<std::string, ModelRecord> m_Models;
  std::unordered_map<std::string, TextureRecord> m_Textures;
  std::unordered_map<std::string, EnvironmentRecord> m_Environments;
  std::unordered_map<std::string, MaterialRecord> m_Materials;
  AssetImporter m_Importer;
  SceneSerializer m_SceneSerializer;
};
} // namespace Engine

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

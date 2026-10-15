#include "assets/include/AssetManager.h"
#include "runtime/include/LogEngine.h"
#include "runtime/include/LogMacros.h"

#include <algorithm>
#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <random>
#include <ranges>
#include <string_view>
#include <vector>

namespace Engine {
namespace {
using Json = nlohmann::json;
constexpr uint32_t ModelImporterVersion = 1;
constexpr uint32_t TextureImporterVersion = 1;

auto IsGuid(const std::string& guid) -> bool {
  if (guid.size() != 36) {
    return false;
  }
  for (size_t i = 0; i < guid.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (guid.at(i) != '-') {
        return false;
      }
    } else if (std::isxdigit(static_cast<unsigned char>(guid.at(i))) == 0) {
      return false;
    }
  }
  return true;
}

auto NewGuid() -> std::string {
  std::array<uint8_t, 16> bytes{};
  std::random_device random;
  for (auto& byte : bytes) {
    byte = static_cast<uint8_t>(random());
  }
  bytes.at(6) = static_cast<uint8_t>((bytes.at(6) & 0x0f) | 0x40);
  bytes.at(8) = static_cast<uint8_t>((bytes.at(8) & 0x3f) | 0x80);
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result;
  result.reserve(36);
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      result.push_back('-');
    }
    result.push_back(digits.at(bytes.at(i) >> 4));
    result.push_back(digits.at(bytes.at(i) & 0x0f));
  }
  return result;
}

auto IsWithin(const std::filesystem::path& root, const std::filesystem::path& path) -> bool {
  const auto relative = path.lexically_relative(root);
  return !relative.empty() && *relative.begin() != "..";
}

auto ResolveProjectPath(const std::filesystem::path& root, const std::filesystem::path& path, std::string& error)
    -> std::optional<std::filesystem::path> {
  if (root.empty()) {
    error = "project root must be set before accessing project files";
    return std::nullopt;
  }
  std::error_code ec;
  auto candidate = (path.is_absolute() ? path : root / path).lexically_normal();
  if (!IsWithin(root, candidate)) {
    error = path.generic_string() + ": path is outside the project root";
    return std::nullopt;
  }

  auto existingAncestor = candidate;
  std::vector<std::filesystem::path> missingComponents;
  while (!std::filesystem::exists(existingAncestor, ec) && !ec) {
    if (existingAncestor.empty() || existingAncestor == existingAncestor.root_path()) {
      break;
    }
    missingComponents.push_back(existingAncestor.filename());
    existingAncestor = existingAncestor.parent_path();
  }
  if (ec) {
    error = path.generic_string() + ": could not inspect path: " + ec.message();
    return std::nullopt;
  }
  if (existingAncestor.empty() || !std::filesystem::exists(existingAncestor, ec) || ec) {
    error = path.generic_string() + ": no existing parent directory could be resolved";
    return std::nullopt;
  }
  auto resolved = std::filesystem::weakly_canonical(existingAncestor, ec);
  if (ec) {
    error = path.generic_string() + ": could not resolve parent path: " + ec.message();
    return std::nullopt;
  }
  for (const auto& component : std::views::reverse(missingComponents)) {
    resolved /= component;
  }
  resolved = resolved.lexically_normal();
  if (!IsWithin(root, resolved)) {
    error = path.generic_string() + ": resolved path is outside the project root";
    return std::nullopt;
  }
  return resolved;
}

auto ReadMetadata(const std::filesystem::path& path, std::string& guid, std::string& error,
                  const std::string& expectedType = "model") -> bool {
  try {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      error = path.generic_string() + ": could not open metadata";
      return false;
    }
    const Json metadata = Json::parse(input);
    const std::string importer = expectedType == "model" ? "tinygltf" : "stb_image";
    const uint32_t importerVersion = expectedType == "model" ? ModelImporterVersion : TextureImporterVersion;
    if (!metadata.is_object() || metadata.value("assetType", std::string{}) != expectedType ||
        metadata.value("schemaVersion", 0U) != 1 || metadata.value("importer", std::string{}) != importer ||
        metadata.value("importerVersion", 0U) != importerVersion || !metadata.contains("guid") ||
        !metadata.at("guid").is_string()) {
      error = path.generic_string() + ": expected model metadata with a GUID";
      return false;
    }
    guid = metadata.at("guid").get<std::string>();
    if (!IsGuid(guid)) {
      error = path.generic_string() + ": metadata contains an invalid GUID";
      return false;
    }
    return true;
  } catch (const std::exception& exception) {
    error = path.generic_string() + ": invalid metadata: " + exception.what();
    return false;
  }
}

auto WriteMetadata(const std::filesystem::path& path, const std::string& guid, std::string& error,
                   const std::string& assetType = "model") -> bool {
  const auto temporary = std::filesystem::path(path.generic_string() + ".tmp." + NewGuid());
  try {
    {
      std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      if (!output) {
        error = path.generic_string() + ": could not create metadata temporary file";
        return false;
      }
      const std::string importer = assetType == "model" ? "tinygltf" : "stb_image";
      const uint32_t importerVersion = assetType == "model" ? ModelImporterVersion : TextureImporterVersion;
      output << Json{{"schemaVersion", 1},
                     {"assetType", assetType},
                     {"guid", guid},
                     {"importer", importer},
                     {"importerVersion", importerVersion},}
                    .dump(2)
             << '\n';
      output.flush();
      if (!output) {
        error = path.generic_string() + ": could not write metadata";
        output.close();
        std::filesystem::remove(temporary);
        return false;
      }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
      std::filesystem::remove(temporary);
      error = path.generic_string() + ": metadata replacement failed: " + ec.message();
      return false;
    }
    return true;
  } catch (const std::exception& exception) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    error = path.generic_string() + ": metadata write failed: " + exception.what();
    return false;
  }
}

auto HasDuplicateGuid(const std::filesystem::path& root, const std::filesystem::path& ownMetadata,
                      const std::string& guid, std::string& error) -> bool {
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
    if (!it->is_regular_file(ec) || it->path().extension() != ".meta" || it->path() == ownMetadata) {
      continue;
    }
    try {
      std::ifstream input(it->path(), std::ios::binary);
      if (!input) {
        error = it->path().generic_string() + ": could not inspect metadata while checking GUID uniqueness";
        return true;
      }
      const Json metadata = Json::parse(input);
      if (metadata.is_object() && metadata.contains("guid") && metadata.at("guid").is_string() &&
          metadata.at("guid").get<std::string>() == guid) {
        error = "duplicate asset GUID " + guid + " in " + it->path().generic_string();
        return true;
      }
    } catch (const std::exception& exception) {
      error = it->path().generic_string() + ": invalid metadata while checking GUID uniqueness: " + exception.what();
      return true;
    }
  }
  if (ec) {
    error = root.generic_string() + ": could not scan project metadata: " + ec.message();
    return true;
  }
  return false;
}

auto DecodeUriPath(const std::string& uri, std::string& path, std::string& error) -> bool {
  path.clear();
  for (size_t i = 0; i < uri.size(); ++i) {
    if (uri.at(i) != '%') {
      path.push_back(uri.at(i));
      continue;
    }
    if (i + 2 >= uri.size()) {
      error = "external glTF URI contains an incomplete percent escape";
      return false;
    }
    const auto hex = [](char c) -> int {
      if (c >= '0' && c <= '9') {
        return c - '0';
      }
      if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
      }
      if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
      }
      return -1;
    };
    const int high = hex(uri.at(i + 1));
    const int low = hex(uri.at(i + 2));
    if (high < 0 || low < 0 || (high == 0 && low == 0)) {
      error = "external glTF URI contains an invalid percent escape";
      return false;
    }
    path.push_back(static_cast<char>((high << 4) | low));
    i += 2;
  }
  return true;
}

auto ReadModelDocument(const std::filesystem::path& sourcePath, Json& document, std::string& error) -> bool {
  try {
    std::ifstream input(sourcePath, std::ios::binary);
    if (sourcePath.extension() == ".glb") {
      std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
      const auto read32 = [&bytes](size_t offset) -> uint32_t {
        return static_cast<uint32_t>(bytes.at(offset)) | (static_cast<uint32_t>(bytes.at(offset + 1)) << 8) |
               (static_cast<uint32_t>(bytes.at(offset + 2)) << 16) |
               (static_cast<uint32_t>(bytes.at(offset + 3)) << 24);
      };
      if (bytes.size() < 20 || read32(0) != 0x46546c67 || read32(4) != 2 || read32(8) > bytes.size()) {
        error = sourcePath.generic_string() + ": malformed GLB header";
        return false;
      }
      size_t offset = 12;
      bool foundJson = false;
      while (offset + 8 <= read32(8)) {
        const uint32_t length = read32(offset);
        const uint32_t type = read32(offset + 4);
        offset += 8;
        if (length > read32(8) - offset) {
          break;
        }
        if (type == 0x4e4f534a) {
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) // GLB JSON chunks are text stored as bytes.
          const char* jsonChunk = reinterpret_cast<const char*>(bytes.data() + offset);
          const std::string jsonBytes(jsonChunk, length);
          document = Json::parse(jsonBytes);
          foundJson = true;
          break;
        }
        offset += length;
      }
      if (!foundJson) {
        error = sourcePath.generic_string() + ": GLB contains no JSON chunk";
        return false;
      }
    } else {
      document = Json::parse(input);
    }
  } catch (const std::exception& exception) {
    error = sourcePath.generic_string() + ": could not inspect glTF resource URIs: " + exception.what();
    return false;
  }
  return true;
}

auto ValidateResourceCollection(const std::filesystem::path& sourcePath, const std::filesystem::path& projectRoot,
                                const Json& document, const char* collection, std::string& error) -> bool {
  if (!document.is_object() || !document.contains(collection) || !document.at(collection).is_array()) {
    return true;
  }
  for (const auto& item : document.at(collection)) {
    if (!item.is_object() || !item.contains("uri") || !item.at("uri").is_string()) {
      continue;
    }
    const std::string uri = item.at("uri").get<std::string>();
    if (uri.starts_with("data:")) {
      continue;
    }
    std::string decoded;
    if (!DecodeUriPath(uri, decoded, error)) {
      return false;
    }
    const std::filesystem::path relative(decoded);
    if (relative.is_absolute() || decoded.contains(':')) {
      error = sourcePath.generic_string() + ": external glTF URI must be project-relative: " + uri;
      return false;
    }
    std::error_code errorCode;
    const auto resolved = std::filesystem::weakly_canonical(sourcePath.parent_path() / relative, errorCode);
    if (errorCode || !IsWithin(projectRoot, resolved)) {
      error = sourcePath.generic_string() + ": external glTF URI resolves outside the project root: " + uri;
      return false;
    }
  }
  return true;
}

auto ValidateModelUris(const std::filesystem::path& sourcePath, const std::filesystem::path& projectRoot,
                       std::string& error) -> bool {
  Json document;
  if (!ReadModelDocument(sourcePath, document, error)) {
    return false;
  }
  for (const char* collection : {"buffers", "images"}) {
    if (!ValidateResourceCollection(sourcePath, projectRoot, document, collection, error)) {
      return false;
    }
  }
  return true;
}
} // namespace

AssetManager::AssetManager(const std::filesystem::path& projectRoot) {
  std::string ignored;
  SetProjectRoot(projectRoot, ignored);
}

auto AssetManager::SetProjectRoot(const std::filesystem::path& projectRoot, std::string& error) -> bool {
  Log::ScopedOperation operation;
  ENGINE_LOG_INFO("Asset", "Project root update started | requested_path: {}", projectRoot.generic_string());
  error.clear();
  std::error_code ec;
  auto root = std::filesystem::weakly_canonical(projectRoot, ec);
  if (ec || !std::filesystem::is_directory(root, ec) || ec) {
    error = projectRoot.generic_string() + ": project root is not an accessible directory";
    ENGINE_LOG_ERROR("Asset", "Project root update failed | requested_path: {} | error: {}",
                     projectRoot.generic_string(), error);
    return false;
  }
  m_ProjectRoot = std::move(root);
  m_Models.clear();
  m_Textures.clear();
  ENGINE_LOG_INFO("Asset",
                  "Project root updated | canonical_path: {} | model_cache_cleared: true | texture_cache_cleared: true",
                  m_ProjectRoot.generic_string());
  return true;
}

auto AssetManager::ImportModel(const std::filesystem::path& sourcePath) -> Result<std::string> {
  Log::ScopedOperation operation;
  ENGINE_LOG_INFO("Asset", "Model import started | path: {}", sourcePath.generic_string());
  std::string guid;
  std::string error;
  if (!ImportModel(sourcePath, guid, error)) {
    ENGINE_LOG_ERROR_CODE("Asset", "asset.model.import_failed", "Model import failed | path: {} | error: {}",
                          sourcePath.generic_string(), error);
    return std::unexpected(MakeError(std::move(error)));
  }
  ENGINE_LOG_INFO("Asset", "Model imported | path: {} | guid: {}", sourcePath.generic_string(), guid);
  return guid;
}

auto AssetManager::ImportTexture(const std::filesystem::path& sourcePath) -> Result<std::string> {
  Log::ScopedOperation operation;
  ENGINE_LOG_INFO("Asset", "Texture import started | path: {}", sourcePath.generic_string());
  std::string guid;
  std::string error;
  if (!ImportTexture(sourcePath, guid, error)) {
    ENGINE_LOG_ERROR_CODE("Asset", "asset.texture.import_failed", "Texture import failed | path: {} | error: {}",
                          sourcePath.generic_string(), error);
    return std::unexpected(MakeError(std::move(error)));
  }
  ENGINE_LOG_INFO("Asset", "Texture imported | path: {} | guid: {}", sourcePath.generic_string(), guid);
  return guid;
}

auto AssetManager::ImportModel(const std::filesystem::path& sourcePath, std::string& guid, std::string& error) -> bool {
  guid.clear();
  error.clear();
  if (m_ProjectRoot.empty()) {
    error = "project root must be set before importing assets";
    return false;
  }
  std::error_code ec;
  const auto candidate = sourcePath.is_absolute() ? sourcePath : m_ProjectRoot / sourcePath;
  const auto canonicalSource = std::filesystem::weakly_canonical(candidate, ec);
  if (ec || !std::filesystem::is_regular_file(canonicalSource, ec) || ec) {
    error = sourcePath.generic_string() + ": source file does not exist or is inaccessible";
    return false;
  }
  if (!IsWithin(m_ProjectRoot, canonicalSource)) {
    error = canonicalSource.generic_string() + ": source file is outside the project root";
    return false;
  }
  const auto extension = canonicalSource.extension().string();
  if (extension != ".gltf" && extension != ".glb") {
    error = canonicalSource.generic_string() + ": only .gltf and .glb model assets are supported";
    return false;
  }

  const auto metadataPath = std::filesystem::path(canonicalSource.generic_string() + ".meta");
  std::string assetGuid;
  const bool hasMetadata = std::filesystem::exists(metadataPath, ec);
  if (ec) {
    error = metadataPath.generic_string() + ": could not inspect metadata: " + ec.message();
    return false;
  }
  if (hasMetadata) {
    if (!ReadMetadata(metadataPath, assetGuid, error)) {
      return false;
    }
  } else {
    assetGuid = NewGuid();
  }

  if (HasDuplicateGuid(m_ProjectRoot, metadataPath, assetGuid, error)) {
    return false;
  }

  if (!ValidateModelUris(canonicalSource, m_ProjectRoot, error)) {
    return false;
  }
  auto imported = AssetImporter::ImportGltf(canonicalSource);
  if (!imported) {
    error = imported.error;
    return false;
  }
  imported.model.importWarnings = std::move(imported.warnings);
  if (!hasMetadata && !WriteMetadata(metadataPath, assetGuid, error)) {
    return false;
  }

  uint64_t generation = 1;
  if (const auto existing = m_Models.find(assetGuid); existing != m_Models.end()) {
    if (existing->second.generation == std::numeric_limits<uint64_t>::max()) {
      error = "asset import generation is exhausted for GUID " + assetGuid;
      return false;
    }
    generation = existing->second.generation + 1;
  }
  guid = std::move(assetGuid);
  m_Models.insert_or_assign(guid, ModelRecord{
                                      .sourcePath = canonicalSource,
                                      .model = std::make_shared<const ImportedModel>(std::move(imported.model)),
                                      .generation = generation,
                                  });
  return true;
}

auto AssetManager::ImportTexture(const std::filesystem::path& sourcePath, std::string& guid, std::string& error)
    -> bool {
  guid.clear();
  error.clear();
  if (m_ProjectRoot.empty()) {
    error = "project root must be set before importing assets";
    return false;
  }
  std::error_code ec;
  const auto candidate = sourcePath.is_absolute() ? sourcePath : m_ProjectRoot / sourcePath;
  const auto canonicalSource = std::filesystem::weakly_canonical(candidate, ec);
  if (ec || !std::filesystem::is_regular_file(canonicalSource, ec) || ec) {
    error = sourcePath.generic_string() + ": source file does not exist or is inaccessible";
    return false;
  }
  if (!IsWithin(m_ProjectRoot, canonicalSource)) {
    error = canonicalSource.generic_string() + ": source file is outside the project root";
    return false;
  }
  auto imported = AssetImporter::ImportImage(canonicalSource);
  if (!imported) {
    error = imported.error;
    return false;
  }
  const auto metadataPath = std::filesystem::path(canonicalSource.generic_string() + ".meta");
  const bool hasMetadata = std::filesystem::exists(metadataPath, ec);
  if (ec) {
    error = metadataPath.generic_string() + ": could not inspect metadata: " + ec.message();
    return false;
  }
  std::string assetGuid;
  if (hasMetadata) {
    if (!ReadMetadata(metadataPath, assetGuid, error, "texture")) {
      return false;
    }
  } else {
    assetGuid = NewGuid();
  }
  if (HasDuplicateGuid(m_ProjectRoot, metadataPath, assetGuid, error)) {
    return false;
  }
  if (!hasMetadata && !WriteMetadata(metadataPath, assetGuid, error, "texture")) {
    return false;
  }

  uint64_t generation = 1;
  if (const auto existing = m_Textures.find(assetGuid); existing != m_Textures.end()) {
    if (existing->second.generation == std::numeric_limits<uint64_t>::max()) {
      error = "asset import generation is exhausted for GUID " + assetGuid;
      return false;
    }
    generation = existing->second.generation + 1;
  }
  guid = assetGuid;
  m_Textures.insert_or_assign(guid, TextureRecord{
                                        .sourcePath = canonicalSource,
                                        .image = std::make_shared<const ImportedImage>(std::move(imported.image)),
                                        .generation = generation,
                                    });
  return true;
}

auto AssetManager::LoadModel(const std::string& guid, std::string& error) -> bool {
  Log::ScopedOperation operation;
  error.clear();
  if (m_Models.contains(guid)) {
    ENGINE_LOG_TRACE("Asset", "Model cache hit | guid: {} | generation: {}", guid, m_Models.at(guid).generation);
    return true;
  }
  ENGINE_LOG_DEBUG("Asset", "Model cache miss | guid: {} | project_root: {}", guid, m_ProjectRoot.generic_string());
  const auto fail = [&] -> bool {
    ENGINE_LOG_ERROR_CODE("Asset", "asset.model.load_failed",
                          "Model load failed | guid: {} | project_root: {} | error: {}", guid,
                          m_ProjectRoot.generic_string(), error);
    return false;
  };
  if (m_ProjectRoot.empty() || !IsGuid(guid)) {
    error = m_ProjectRoot.empty() ? "project root must be set before loading assets" : "asset GUID is invalid";
    return fail();
  }

  std::optional<std::filesystem::path> sourcePath;
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(m_ProjectRoot, ec), end; it != end && !ec; it.increment(ec)) {
    if (!it->is_regular_file(ec) || it->path().extension() != ".meta") {
      continue;
    }
    std::string metadataGuid;
    std::string metadataError;
    if (!ReadMetadata(it->path(), metadataGuid, metadataError) || metadataGuid != guid) {
      continue;
    }
    if (sourcePath) {
      error = "duplicate asset GUID " + guid + " in " + it->path().generic_string();
      return fail();
    }
    auto source = it->path();
    source.replace_extension();
    sourcePath = std::move(source);
  }
  if (ec) {
    error = m_ProjectRoot.generic_string() + ": could not scan project metadata: " + ec.message();
    return fail();
  }
  if (!sourcePath) {
    error = "asset GUID " + guid + " was not found in project metadata";
    return fail();
  }

  std::string loadedGuid;
  if (!ImportModel(*sourcePath, loadedGuid, error)) {
    return fail();
  }
  if (loadedGuid != guid) {
    m_Models.erase(loadedGuid);
    error = sourcePath->generic_string() + ": resolved sidecar GUID changed during load";
    return fail();
  }
  ENGINE_LOG_INFO("Asset", "Model loaded | guid: {} | source_path: {} | generation: {}", guid,
                  sourcePath->generic_string(), m_Models.at(guid).generation);
  return true;
}

auto AssetManager::LoadTexture(const std::string& guid, std::string& error) -> bool {
  Log::ScopedOperation operation;
  error.clear();
  if (m_Textures.contains(guid)) {
    ENGINE_LOG_TRACE("Asset", "Texture cache hit | guid: {} | generation: {}", guid, m_Textures.at(guid).generation);
    return true;
  }
  ENGINE_LOG_DEBUG("Asset", "Texture cache miss | guid: {} | project_root: {}", guid, m_ProjectRoot.generic_string());
  const auto fail = [&] -> bool {
    ENGINE_LOG_ERROR_CODE("Asset", "asset.texture.load_failed",
                          "Texture load failed | guid: {} | project_root: {} | error: {}", guid,
                          m_ProjectRoot.generic_string(), error);
    return false;
  };
  if (m_ProjectRoot.empty() || !IsGuid(guid)) {
    error = m_ProjectRoot.empty() ? "project root must be set before loading assets" : "asset GUID is invalid";
    return fail();
  }
  std::optional<std::filesystem::path> sourcePath;
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(m_ProjectRoot, ec), end; it != end && !ec; it.increment(ec)) {
    if (!it->is_regular_file(ec) || it->path().extension() != ".meta") {
      continue;
    }
    std::string metadataGuid;
    std::string metadataError;
    if (!ReadMetadata(it->path(), metadataGuid, metadataError, "texture") || metadataGuid != guid) {
      continue;
    }
    if (sourcePath) {
      error = "duplicate asset GUID " + guid + " in " + it->path().generic_string();
      return fail();
    }
    auto source = it->path();
    source.replace_extension();
    sourcePath = std::move(source);
  }
  if (ec) {
    error = m_ProjectRoot.generic_string() + ": could not scan project metadata: " + ec.message();
    return fail();
  }
  if (!sourcePath) {
    error = "texture GUID " + guid + " was not found in project metadata";
    return fail();
  }
  std::string loadedGuid;
  if (!ImportTexture(*sourcePath, loadedGuid, error)) {
    return fail();
  }
  if (loadedGuid != guid) {
    m_Textures.erase(loadedGuid);
    error = sourcePath->generic_string() + ": resolved sidecar GUID changed during load";
    return fail();
  }
  ENGINE_LOG_INFO("Asset", "Texture loaded | guid: {} | source_path: {} | generation: {}", guid,
                  sourcePath->generic_string(), m_Textures.at(guid).generation);
  return true;
}

auto AssetManager::RefreshRegistry(std::vector<std::string>& warnings, std::string& error) -> bool {
  Log::ScopedOperation operation;
  warnings.clear();
  error.clear();
  ENGINE_LOG_INFO("Asset", "Asset registry refresh started | project_root: {}", m_ProjectRoot.generic_string());
  if (m_ProjectRoot.empty()) {
    error = "project root must be set before enumerating assets";
    ENGINE_LOG_ERROR("Asset", "Asset registry refresh rejected | project_root_length: 0 | error: {}", error);
    return false;
  }
  std::unordered_map<std::string, std::filesystem::path> seen;
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(m_ProjectRoot, ec), end; it != end && !ec; it.increment(ec)) {
    if (!it->is_regular_file(ec) || it->path().extension() != ".meta") {
      continue;
    }
    try {
      std::ifstream input(it->path(), std::ios::binary);
      const Json metadata = Json::parse(input);
      const std::string assetType = metadata.value("assetType", std::string{});
      std::string guid;
      std::string metadataError;
      if ((assetType != "model" && assetType != "texture") ||
          !ReadMetadata(it->path(), guid, metadataError, assetType)) {
        warnings.push_back(it->path().generic_string() + ": " +
                           (metadataError.empty() ? "unsupported asset metadata type" : metadataError));
        continue;
      }
      if (const auto duplicate = seen.find(guid); duplicate != seen.end()) {
        warnings.push_back("duplicate asset GUID " + guid + " in " + duplicate->second.generic_string() + " and " +
                           it->path().generic_string());
        continue;
      }
      seen.emplace(guid, it->path());
      const bool loaded = assetType == "model" ? LoadModel(guid, metadataError) : LoadTexture(guid, metadataError);
      if (!loaded) {
        warnings.push_back(metadataError);
      }
    } catch (const std::exception& exception) {
      warnings.push_back(it->path().generic_string() + ": invalid metadata: " + exception.what());
    }
  }
  if (ec) {
    error = m_ProjectRoot.generic_string() + ": could not enumerate project assets: " + ec.message();
    ENGINE_LOG_ERROR_CODE("Asset", "asset.registry.refresh_failed",
                          "Asset registry refresh failed | project_root: {} | error: {}",
                          m_ProjectRoot.generic_string(), error);
    return false;
  }
  ENGINE_LOG_INFO(
      "Asset", "Asset registry refreshed | project_root: {} | model_count: {} | texture_count: {} | warning_count: {}",
      m_ProjectRoot.generic_string(), m_Models.size(), m_Textures.size(), warnings.size());
  return true;
}

auto AssetManager::GetModel(const std::string& guid) const -> std::shared_ptr<const ImportedModel> {
  const auto it = m_Models.find(guid);
  return it == m_Models.end() ? nullptr : it->second.model;
}

auto AssetManager::GetTexture(const std::string& guid) const -> std::shared_ptr<const ImportedImage> {
  const auto it = m_Textures.find(guid);
  return it == m_Textures.end() ? nullptr : it->second.image;
}

auto AssetManager::GetGeneration(const std::string& guid) const -> std::optional<uint64_t> {
  const auto it = m_Models.find(guid);
  if (it != m_Models.end()) {
    return it->second.generation;
  }
  const auto texture = m_Textures.find(guid);
  return texture == m_Textures.end() ? std::nullopt : std::optional(texture->second.generation);
}

auto AssetManager::GetSourcePath(const std::string& guid) const -> std::optional<std::filesystem::path> {
  const auto it = m_Models.find(guid);
  if (it != m_Models.end()) {
    return it->second.sourcePath;
  }
  const auto texture = m_Textures.find(guid);
  return texture == m_Textures.end() ? std::nullopt : std::optional(texture->second.sourcePath);
}

auto AssetManager::GetKnownModels() const -> std::vector<AssetManager::ModelAssetInfo> {
  std::vector<ModelAssetInfo> result;
  result.reserve(m_Models.size());
  for (const auto& [guid, record] : m_Models) {
    result.push_back({
        .guid = guid,
        .name = record.sourcePath.stem().string(),
        .sourcePath = record.sourcePath.lexically_relative(m_ProjectRoot),
    });
  }
  std::ranges::sort(result, [](const auto& left, const auto& right) -> auto {
    return left.name == right.name ? left.guid < right.guid : left.name < right.name;
  });
  return result;
}

auto AssetManager::GetKnownTextures() const -> std::vector<AssetManager::TextureAssetInfo> {
  std::vector<TextureAssetInfo> result;
  result.reserve(m_Textures.size());
  for (const auto& [guid, record] : m_Textures) {
    result.push_back({
        .guid = guid,
        .name = record.sourcePath.stem().string(),
        .sourcePath = record.sourcePath.lexically_relative(m_ProjectRoot),
    });
  }
  std::ranges::sort(result, [](const auto& left, const auto& right) -> auto {
    return left.name == right.name ? left.guid < right.guid : left.name < right.name;
  });
  return result;
}

auto AssetManager::GetEmbeddedImageIdentity(const std::string& modelGuid, std::size_t imageIndex) -> std::string {
  return modelGuid + "/image/" + std::to_string(imageIndex);
}

auto AssetManager::LoadScene(const std::filesystem::path& path) const -> SceneDocumentResult {
  std::string error;
  const auto resolvedPath = ResolveProjectPath(m_ProjectRoot, path, error);
  if (!resolvedPath) {
    SceneDocumentResult result;
    result.error = std::move(error);
    return result;
  }
  return SceneSerializer::Load(*resolvedPath);
}

auto AssetManager::SaveScene(const std::filesystem::path& path, const SceneDocument& document, std::string& error) const
    -> bool {
  const auto resolvedPath = ResolveProjectPath(m_ProjectRoot, path, error);
  return resolvedPath && SceneSerializer::SaveAtomic(*resolvedPath, document, error);
}
} // namespace Engine

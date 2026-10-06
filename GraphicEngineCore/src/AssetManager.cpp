#include "AssetManager.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <random>
#include <vector>

namespace Engine {
namespace {
	using Json = nlohmann::json;
	constexpr uint32_t ModelImporterVersion = 1;
	constexpr uint32_t TextureImporterVersion = 1;

	bool IsGuid(const std::string& guid) {
		if (guid.size() != 36) return false;
		for (size_t i = 0; i < guid.size(); ++i) {
			if (i == 8 || i == 13 || i == 18 || i == 23) {
				if (guid[i] != '-') return false;
			} else if (!std::isxdigit(static_cast<unsigned char>(guid[i]))) {
				return false;
			}
		}
		return true;
	}

	std::string NewGuid() {
		std::array<uint8_t, 16> bytes{};
		std::random_device random;
		for (auto& byte : bytes) byte = static_cast<uint8_t>(random());
		bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0f) | 0x40);
		bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3f) | 0x80);
		constexpr char digits[] = "0123456789abcdef";
		std::string result;
		result.reserve(36);
		for (size_t i = 0; i < bytes.size(); ++i) {
			if (i == 4 || i == 6 || i == 8 || i == 10) result.push_back('-');
			result.push_back(digits[bytes[i] >> 4]);
			result.push_back(digits[bytes[i] & 0x0f]);
		}
		return result;
	}

	bool IsWithin(const std::filesystem::path& root, const std::filesystem::path& path) {
		const auto relative = path.lexically_relative(root);
		return !relative.empty() && *relative.begin() != "..";
	}

	std::optional<std::filesystem::path> ResolveProjectPath(
		const std::filesystem::path& root,
		const std::filesystem::path& path,
		std::string& error) {
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
			if (existingAncestor.empty() || existingAncestor == existingAncestor.root_path()) break;
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
		for (auto it = missingComponents.rbegin(); it != missingComponents.rend(); ++it) resolved /= *it;
		resolved = resolved.lexically_normal();
		if (!IsWithin(root, resolved)) {
			error = path.generic_string() + ": resolved path is outside the project root";
			return std::nullopt;
		}
		return resolved;
	}

	bool ReadMetadata(const std::filesystem::path& path, std::string& guid, std::string& error,
		const std::string& expectedType = "model") {
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
				metadata.value("schemaVersion", 0u) != 1 ||
				metadata.value("importer", std::string{}) != importer ||
				metadata.value("importerVersion", 0u) != importerVersion ||
				!metadata.contains("guid") || !metadata["guid"].is_string()) {
				error = path.generic_string() + ": expected model metadata with a GUID";
				return false;
			}
			guid = metadata["guid"].get<std::string>();
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

	bool WriteMetadata(const std::filesystem::path& path, const std::string& guid, std::string& error,
		const std::string& assetType = "model") {
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
				output << Json{{"schemaVersion", 1}, {"assetType", assetType}, {"guid", guid},
					{"importer", importer}, {"importerVersion", importerVersion}}.dump(2) << '\n';
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

	bool HasDuplicateGuid(const std::filesystem::path& root, const std::filesystem::path& ownMetadata,
		const std::string& guid, std::string& error) {
		std::error_code ec;
		for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
			if (!it->is_regular_file(ec) || it->path().extension() != ".meta" || it->path() == ownMetadata) continue;
			try {
				std::ifstream input(it->path(), std::ios::binary);
				if (!input) {
					error = it->path().generic_string() + ": could not inspect metadata while checking GUID uniqueness";
					return true;
				}
				const Json metadata = Json::parse(input);
				if (metadata.is_object() && metadata.contains("guid") && metadata["guid"].is_string() &&
					metadata["guid"].get<std::string>() == guid) {
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

	bool DecodeUriPath(const std::string& uri, std::string& path, std::string& error) {
		path.clear();
		for (size_t i = 0; i < uri.size(); ++i) {
			if (uri[i] != '%') {
				path.push_back(uri[i]);
				continue;
			}
			if (i + 2 >= uri.size()) {
				error = "external glTF URI contains an incomplete percent escape";
				return false;
			}
			const auto hex = [](char c) -> int {
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				return -1;
			};
			const int high = hex(uri[i + 1]), low = hex(uri[i + 2]);
			if (high < 0 || low < 0 || (high == 0 && low == 0)) {
				error = "external glTF URI contains an invalid percent escape";
				return false;
			}
			path.push_back(static_cast<char>((high << 4) | low));
			i += 2;
		}
		return true;
	}

	bool ValidateModelUris(const std::filesystem::path& sourcePath, const std::filesystem::path& projectRoot,
		std::string& error) {
		Json document;
		try {
			std::ifstream input(sourcePath, std::ios::binary);
			if (sourcePath.extension() == ".glb") {
				std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
				const auto read32 = [&bytes](size_t offset) -> uint32_t {
					return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
						(static_cast<uint32_t>(bytes[offset + 2]) << 16) | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
				};
				if (bytes.size() < 20 || read32(0) != 0x46546c67 || read32(4) != 2 || read32(8) > bytes.size()) {
					error = sourcePath.generic_string() + ": malformed GLB header";
					return false;
				}
				size_t offset = 12;
				bool foundJson = false;
				while (offset + 8 <= read32(8)) {
					const uint32_t length = read32(offset), type = read32(offset + 4);
					offset += 8;
					if (length > read32(8) - offset) break;
					if (type == 0x4e4f534a) {
						const std::string jsonBytes(reinterpret_cast<const char*>(bytes.data() + offset), length);
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

		for (const char* collection : {"buffers", "images"}) {
			if (!document.is_object() || !document.contains(collection) || !document[collection].is_array()) continue;
			for (const auto& item : document[collection]) {
				if (!item.is_object() || !item.contains("uri") || !item["uri"].is_string()) continue;
				const std::string uri = item["uri"].get<std::string>();
				if (uri.starts_with("data:")) continue;
				std::string decoded;
				if (!DecodeUriPath(uri, decoded, error)) return false;
				const std::filesystem::path relative(decoded);
				if (relative.is_absolute() || decoded.find(':') != std::string::npos) {
					error = sourcePath.generic_string() + ": external glTF URI must be project-relative: " + uri;
					return false;
				}
				std::error_code ec;
				const auto resolved = std::filesystem::weakly_canonical(sourcePath.parent_path() / relative, ec);
				if (ec || !IsWithin(projectRoot, resolved)) {
					error = sourcePath.generic_string() + ": external glTF URI resolves outside the project root: " + uri;
					return false;
				}
			}
		}
		return true;
	}
}

	AssetManager::AssetManager(std::filesystem::path projectRoot) {
		std::string ignored;
		SetProjectRoot(projectRoot, ignored);
	}

	bool AssetManager::SetProjectRoot(const std::filesystem::path& projectRoot, std::string& error) {
		error.clear();
		std::error_code ec;
		auto root = std::filesystem::weakly_canonical(projectRoot, ec);
		if (ec || !std::filesystem::is_directory(root, ec) || ec) {
			error = projectRoot.generic_string() + ": project root is not an accessible directory";
			return false;
		}
		m_ProjectRoot = std::move(root);
		m_Models.clear();
		m_Textures.clear();
		return true;
	}

	bool AssetManager::ImportModel(const std::filesystem::path& sourcePath, std::string& guid, std::string& error) {
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
			if (!ReadMetadata(metadataPath, assetGuid, error)) return false;
		} else {
			assetGuid = NewGuid();
		}

		if (HasDuplicateGuid(m_ProjectRoot, metadataPath, assetGuid, error)) return false;

		if (!ValidateModelUris(canonicalSource, m_ProjectRoot, error)) return false;
		auto imported = m_Importer.ImportGltf(canonicalSource);
		if (!imported) {
			error = imported.error;
			return false;
		}
		if (!hasMetadata && !WriteMetadata(metadataPath, assetGuid, error)) return false;

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
			canonicalSource,
			std::make_shared<const ImportedModel>(std::move(imported.model)),
			generation
		});
		return true;
	}

	bool AssetManager::ImportTexture(const std::filesystem::path& sourcePath, std::string& guid, std::string& error) {
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
		auto imported = m_Importer.ImportImage(canonicalSource);
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
			if (!ReadMetadata(metadataPath, assetGuid, error, "texture")) return false;
		} else {
			assetGuid = NewGuid();
		}
		if (HasDuplicateGuid(m_ProjectRoot, metadataPath, assetGuid, error)) return false;
		if (!hasMetadata && !WriteMetadata(metadataPath, assetGuid, error, "texture")) return false;

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
			canonicalSource,
			std::make_shared<const ImportedImage>(std::move(imported.image)),
			generation
		});
		return true;
	}

	bool AssetManager::LoadModel(const std::string& guid, std::string& error) {
		error.clear();
		if (m_Models.contains(guid)) return true;
		if (m_ProjectRoot.empty() || !IsGuid(guid)) {
			error = m_ProjectRoot.empty() ? "project root must be set before loading assets" : "asset GUID is invalid";
			return false;
		}

		std::optional<std::filesystem::path> sourcePath;
		std::error_code ec;
		for (std::filesystem::recursive_directory_iterator it(m_ProjectRoot, ec), end; it != end && !ec; it.increment(ec)) {
			if (!it->is_regular_file(ec) || it->path().extension() != ".meta") continue;
			std::string metadataGuid;
			std::string metadataError;
			if (!ReadMetadata(it->path(), metadataGuid, metadataError) || metadataGuid != guid) continue;
			if (sourcePath) {
				error = "duplicate asset GUID " + guid + " in " + it->path().generic_string();
				return false;
			}
			auto source = it->path();
			source.replace_extension();
			sourcePath = std::move(source);
		}
		if (ec) {
			error = m_ProjectRoot.generic_string() + ": could not scan project metadata: " + ec.message();
			return false;
		}
		if (!sourcePath) {
			error = "asset GUID " + guid + " was not found in project metadata";
			return false;
		}

		std::string loadedGuid;
		if (!ImportModel(*sourcePath, loadedGuid, error)) return false;
		if (loadedGuid != guid) {
			m_Models.erase(loadedGuid);
			error = sourcePath->generic_string() + ": resolved sidecar GUID changed during load";
			return false;
		}
		return true;
	}

	bool AssetManager::LoadTexture(const std::string& guid, std::string& error) {
		error.clear();
		if (m_Textures.contains(guid)) return true;
		if (m_ProjectRoot.empty() || !IsGuid(guid)) {
			error = m_ProjectRoot.empty() ? "project root must be set before loading assets" : "asset GUID is invalid";
			return false;
		}
		std::optional<std::filesystem::path> sourcePath;
		std::error_code ec;
		for (std::filesystem::recursive_directory_iterator it(m_ProjectRoot, ec), end; it != end && !ec; it.increment(ec)) {
			if (!it->is_regular_file(ec) || it->path().extension() != ".meta") continue;
			std::string metadataGuid;
			std::string metadataError;
			if (!ReadMetadata(it->path(), metadataGuid, metadataError, "texture") || metadataGuid != guid) continue;
			if (sourcePath) {
				error = "duplicate asset GUID " + guid + " in " + it->path().generic_string();
				return false;
			}
			auto source = it->path();
			source.replace_extension();
			sourcePath = std::move(source);
		}
		if (ec) {
			error = m_ProjectRoot.generic_string() + ": could not scan project metadata: " + ec.message();
			return false;
		}
		if (!sourcePath) {
			error = "texture GUID " + guid + " was not found in project metadata";
			return false;
		}
		std::string loadedGuid;
		if (!ImportTexture(*sourcePath, loadedGuid, error)) return false;
		if (loadedGuid != guid) {
			m_Textures.erase(loadedGuid);
			error = sourcePath->generic_string() + ": resolved sidecar GUID changed during load";
			return false;
		}
		return true;
	}

	bool AssetManager::RefreshRegistry(std::vector<std::string>& warnings, std::string& error) {
		warnings.clear();
		error.clear();
		if (m_ProjectRoot.empty()) {
			error = "project root must be set before enumerating assets";
			return false;
		}
		std::unordered_map<std::string, std::filesystem::path> seen;
		std::error_code ec;
		for (std::filesystem::recursive_directory_iterator it(m_ProjectRoot, ec), end; it != end && !ec; it.increment(ec)) {
			if (!it->is_regular_file(ec) || it->path().extension() != ".meta") continue;
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
					warnings.push_back("duplicate asset GUID " + guid + " in " + duplicate->second.generic_string() +
						" and " + it->path().generic_string());
					continue;
				}
				seen.emplace(guid, it->path());
				const bool loaded = assetType == "model" ? LoadModel(guid, metadataError) : LoadTexture(guid, metadataError);
				if (!loaded) warnings.push_back(metadataError);
			} catch (const std::exception& exception) {
				warnings.push_back(it->path().generic_string() + ": invalid metadata: " + exception.what());
			}
		}
		if (ec) {
			error = m_ProjectRoot.generic_string() + ": could not enumerate project assets: " + ec.message();
			return false;
		}
		return true;
	}

	std::shared_ptr<const ImportedModel> AssetManager::GetModel(const std::string& guid) const {
		const auto it = m_Models.find(guid);
		return it == m_Models.end() ? nullptr : it->second.model;
	}

	std::shared_ptr<const ImportedImage> AssetManager::GetTexture(const std::string& guid) const {
		const auto it = m_Textures.find(guid);
		return it == m_Textures.end() ? nullptr : it->second.image;
	}

	std::optional<uint64_t> AssetManager::GetGeneration(const std::string& guid) const {
		const auto it = m_Models.find(guid);
		if (it != m_Models.end()) return it->second.generation;
		const auto texture = m_Textures.find(guid);
		return texture == m_Textures.end() ? std::nullopt : std::optional(texture->second.generation);
	}

	std::optional<std::filesystem::path> AssetManager::GetSourcePath(const std::string& guid) const {
		const auto it = m_Models.find(guid);
		if (it != m_Models.end()) return it->second.sourcePath;
		const auto texture = m_Textures.find(guid);
		return texture == m_Textures.end() ? std::nullopt : std::optional(texture->second.sourcePath);
	}

	std::vector<AssetManager::ModelAssetInfo> AssetManager::GetKnownModels() const {
		std::vector<ModelAssetInfo> result;
		result.reserve(m_Models.size());
		for (const auto& [guid, record] : m_Models) {
			result.push_back({ guid, record.sourcePath.stem().string(), record.sourcePath.lexically_relative(m_ProjectRoot) });
		}
		std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
			return left.name == right.name ? left.guid < right.guid : left.name < right.name;
		});
		return result;
	}

	std::vector<AssetManager::TextureAssetInfo> AssetManager::GetKnownTextures() const {
		std::vector<TextureAssetInfo> result;
		result.reserve(m_Textures.size());
		for (const auto& [guid, record] : m_Textures) {
			result.push_back({ guid, record.sourcePath.stem().string(), record.sourcePath.lexically_relative(m_ProjectRoot) });
		}
		std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
			return left.name == right.name ? left.guid < right.guid : left.name < right.name;
		});
		return result;
	}

	std::string AssetManager::GetEmbeddedImageIdentity(const std::string& modelGuid, std::size_t imageIndex) {
		return modelGuid + "/image/" + std::to_string(imageIndex);
	}

	SceneDocumentResult AssetManager::LoadScene(const std::filesystem::path& path) const {
		std::string error;
		const auto resolvedPath = ResolveProjectPath(m_ProjectRoot, path, error);
		if (!resolvedPath) {
			SceneDocumentResult result;
			result.error = std::move(error);
			return result;
		}
		return m_SceneSerializer.Load(*resolvedPath);
	}

	bool AssetManager::SaveScene(const std::filesystem::path& path, const SceneDocument& document, std::string& error) const {
		const auto resolvedPath = ResolveProjectPath(m_ProjectRoot, path, error);
		return resolvedPath && m_SceneSerializer.SaveAtomic(*resolvedPath, document, error);
	}
}

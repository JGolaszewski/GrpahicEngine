#include "AssetManager.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <fstream>
#include <random>

namespace Engine {
namespace {
	using Json = nlohmann::json;
	constexpr uint32_t ModelImporterVersion = 1;

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
		const auto resolved = std::filesystem::weakly_canonical(path.is_absolute() ? path : root / path, ec);
		if (ec || !IsWithin(root, resolved)) {
			error = path.generic_string() + ": path is outside the project root or cannot be resolved";
			return std::nullopt;
		}
		return resolved;
	}

	bool ReadMetadata(const std::filesystem::path& path, std::string& guid, std::string& error) {
		try {
			std::ifstream input(path, std::ios::binary);
			if (!input) {
				error = path.generic_string() + ": could not open metadata";
				return false;
			}
			const Json metadata = Json::parse(input);
			if (!metadata.is_object() || metadata.value("assetType", std::string{}) != "model" ||
				metadata.value("schemaVersion", 0u) != 1 ||
				metadata.value("importer", std::string{}) != "tinygltf" ||
				metadata.value("importerVersion", 0u) != ModelImporterVersion ||
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

	bool WriteMetadata(const std::filesystem::path& path, const std::string& guid, std::string& error) {
		const auto temporary = std::filesystem::path(path.generic_string() + ".tmp." + NewGuid());
		try {
			{
				std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
				if (!output) {
					error = path.generic_string() + ": could not create metadata temporary file";
					return false;
				}
				output << Json{{"schemaVersion", 1}, {"assetType", "model"}, {"guid", guid},
					{"importer", "tinygltf"}, {"importerVersion", ModelImporterVersion}}.dump(2) << '\n';
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

		for (std::filesystem::recursive_directory_iterator it(m_ProjectRoot, ec), end; it != end && !ec; it.increment(ec)) {
			if (!it->is_regular_file(ec) || it->path() == metadataPath || it->path().extension() != ".meta") continue;
			std::string otherGuid;
			std::string metadataError;
			if (ReadMetadata(it->path(), otherGuid, metadataError) && otherGuid == assetGuid) {
				error = "duplicate asset GUID " + assetGuid + " in " + it->path().generic_string();
				return false;
			}
		}
		if (ec) {
			error = m_ProjectRoot.generic_string() + ": could not scan project metadata: " + ec.message();
			return false;
		}

		auto imported = m_Importer.ImportGltf(canonicalSource);
		if (!imported) {
			error = imported.error;
			return false;
		}
		if (!hasMetadata && !WriteMetadata(metadataPath, assetGuid, error)) return false;

		guid = std::move(assetGuid);
		m_Models.insert_or_assign(guid, ModelRecord{
			canonicalSource,
			std::make_shared<const ImportedModel>(std::move(imported.model))
		});
		return true;
	}

	std::shared_ptr<const ImportedModel> AssetManager::GetModel(const std::string& guid) const {
		const auto it = m_Models.find(guid);
		return it == m_Models.end() ? nullptr : it->second.model;
	}

	std::optional<std::filesystem::path> AssetManager::GetSourcePath(const std::string& guid) const {
		const auto it = m_Models.find(guid);
		return it == m_Models.end() ? std::nullopt : std::optional(it->second.sourcePath);
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

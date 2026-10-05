#pragma once

#include "AssetImporter.h"
#include "CoreExport.h"
#include "SceneDocument.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace Engine {
	class ENGINE_API AssetManager {
	public:
		AssetManager() = default;
		explicit AssetManager(std::filesystem::path projectRoot);

		bool SetProjectRoot(const std::filesystem::path& projectRoot, std::string& error);
		const std::filesystem::path& GetProjectRoot() const noexcept { return m_ProjectRoot; }

		bool ImportModel(const std::filesystem::path& sourcePath, std::string& guid, std::string& error);
		bool LoadModel(const std::string& guid, std::string& error);
		std::shared_ptr<const ImportedModel> GetModel(const std::string& guid) const;
		std::optional<uint64_t> GetGeneration(const std::string& guid) const;
		std::optional<std::filesystem::path> GetSourcePath(const std::string& guid) const;

		SceneDocumentResult LoadScene(const std::filesystem::path& path) const;
		bool SaveScene(const std::filesystem::path& path, const SceneDocument& document, std::string& error) const;

	private:
		struct ModelRecord {
			std::filesystem::path sourcePath;
			std::shared_ptr<const ImportedModel> model;
			uint64_t generation = 1;
		};

		std::filesystem::path m_ProjectRoot;
		std::unordered_map<std::string, ModelRecord> m_Models;
		AssetImporter m_Importer;
		SceneSerializer m_SceneSerializer;
	};
}

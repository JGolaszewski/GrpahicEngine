#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Engine { class AssetManager; struct SceneDocument; }

class EditorSceneDocument {
public:
	const std::filesystem::path& GetPath() const noexcept { return m_Path; }
	void Reset() noexcept { m_Path.clear(); }
	void CommitPath(const std::filesystem::path& path) { m_Path = Canonicalize(path); }
	bool Load(const Engine::AssetManager& assets, const std::filesystem::path& path,
		Engine::SceneDocument& document, std::string& error, std::vector<std::string>& warnings);
	bool Save(const Engine::AssetManager& assets, const Engine::SceneDocument& document,
		const std::filesystem::path& path, std::string& error);

private:
	std::filesystem::path m_Path;
	static std::filesystem::path Canonicalize(const std::filesystem::path& path);
};

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Engine {
	class Application;
	class Scene;
}

class EditorSceneDocument {
public:
	const std::filesystem::path& GetPath() const noexcept { return m_Path; }
	void Reset() noexcept { m_Path.clear(); }
	bool Load(Engine::Application& application, const std::filesystem::path& path,
		std::string& error, std::vector<std::string>& warnings);
	bool Save(const Engine::Application& application, const Engine::Scene& scene,
		const std::filesystem::path& path, std::string& error);

private:
	std::filesystem::path m_Path;
	static std::filesystem::path Canonicalize(const std::filesystem::path& path);
};

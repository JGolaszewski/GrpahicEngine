#include "EditorSceneDocument.h"

#include "AssetManager.h"
#include "SceneDocument.h"

#include <system_error>

std::filesystem::path EditorSceneDocument::Canonicalize(const std::filesystem::path& path) {
	std::error_code error;
	auto canonical = std::filesystem::weakly_canonical(path, error);
	return error ? path.lexically_normal() : canonical;
}

bool EditorSceneDocument::Load(const Engine::AssetManager& assets, const std::filesystem::path& path,
	Engine::SceneDocument& document, std::string& error, std::vector<std::string>& warnings) {
	Engine::SceneDocumentResult loaded = assets.LoadScene(path);
	if (!loaded) {
		error = std::move(loaded.error);
		warnings.clear();
		return false;
	}
	document = std::move(loaded.document);
	warnings = std::move(loaded.warnings);
	return true;
}

bool EditorSceneDocument::Save(const Engine::AssetManager& assets, const Engine::SceneDocument& document,
	const std::filesystem::path& path, std::string& error) {
	if (!assets.SaveScene(path, document, error)) return false;
	m_Path = Canonicalize(path);
	return true;
}

#include "EditorSceneDocument.h"

#include "Application.h"
#include "Scene.h"

#include <system_error>

std::filesystem::path EditorSceneDocument::Canonicalize(const std::filesystem::path& path) {
	std::error_code error;
	auto canonical = std::filesystem::weakly_canonical(path, error);
	return error ? path.lexically_normal() : canonical;
}

bool EditorSceneDocument::Load(Engine::Application& application, const std::filesystem::path& path,
	std::string& error, std::vector<std::string>& warnings) {
	if (!application.LoadScene(path, error, warnings)) return false;
	m_Path = Canonicalize(path);
	return true;
}

bool EditorSceneDocument::Save(const Engine::Application& application, const Engine::Scene& scene,
	const std::filesystem::path& path, std::string& error) {
	if (!application.SaveScene(path, scene, error)) return false;
	m_Path = Canonicalize(path);
	return true;
}

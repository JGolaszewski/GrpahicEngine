#include "AssetManager.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
	void Require(bool condition, const char* message) {
		if (!condition) {
			std::cerr << message << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

	class TestWorkspace {
	public:
		TestWorkspace() {
			const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
			m_Path = std::filesystem::temp_directory_path() /
				("GraphicEngineAssetManagerTests-" + std::to_string(nonce));
			std::filesystem::create_directories(m_Path / "project");
		}
		~TestWorkspace() {
			std::error_code ignored;
			std::filesystem::remove_all(m_Path, ignored);
		}

		const std::filesystem::path& Path() const noexcept { return m_Path; }
		std::filesystem::path Project() const { return m_Path / "project"; }

	private:
		std::filesystem::path m_Path;
	};

	void WriteMinimalModel(const std::filesystem::path& path) {
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"root"}]})";
		Require(static_cast<bool>(output), "could not write minimal glTF fixture");
	}

	void TestImportAndGuidReload(const std::filesystem::path& project) {
		WriteMinimalModel(project / "model.gltf");
		Engine::AssetManager first(project);
		std::string guid;
		std::string error;
		Require(first.ImportModel("model.gltf", guid, error), "model import failed");
		Require(guid.size() == 36, "import did not create a UUID sidecar");
		Require(std::filesystem::exists(project / "model.gltf.meta"), "import did not write adjacent metadata");
		const auto imported = first.GetModel(guid);
		Require(imported != nullptr, "imported model was not retrievable by GUID");
		Require(first.GetSourcePath(guid).has_value(), "source path was not registered");

		Engine::AssetManager reloaded(project);
		Require(reloaded.LoadModel(guid, error), "model GUID reload failed");
		const auto loaded = reloaded.GetModel(guid);
		Require(loaded != nullptr, "GUID reload did not populate the CPU model cache");
		Require(reloaded.GetSourcePath(guid) == first.GetSourcePath(guid), "GUID reload resolved a different source path");
		Require(reloaded.LoadModel(guid, error) && reloaded.GetModel(guid) == loaded,
			"repeated GUID loads did not reuse the cached CPU asset");
	}

	void TestDuplicateGuidAndProjectBoundary(const std::filesystem::path& workspace, const std::filesystem::path& project) {
		const auto source = project / "model.gltf";
		WriteMinimalModel(source);
		Engine::AssetManager importer(project);
		std::string guid;
		std::string error;
		Require(importer.ImportModel(source, guid, error), "model import for duplicate-GUID test failed");
		std::filesystem::copy_file(source, project / "copy.gltf");
		std::filesystem::copy_file(project / "model.gltf.meta", project / "copy.gltf.meta");

		Engine::AssetManager freshRegistry(project);
		Require(!freshRegistry.LoadModel(guid, error), "duplicate GUID sidecars were not rejected");
		Require(error.find("duplicate asset GUID") != std::string::npos, "duplicate GUID error lacks the conflicting identity");

		Engine::AssetManager boundary(project);
		Require(!boundary.ImportModel(workspace / "outside.gltf", guid, error), "asset outside the project root was accepted");
	}

	void TestSceneJsonRoundTrip(const std::filesystem::path& project) {
		Engine::AssetManager assets(project);
		Engine::SceneDocument document;
		Engine::SceneEntityDocument entity;
		entity.id = 17;
		entity.name = "serialized entity";
		entity.modelGuid = "e6f7e48a-1d3b-4f65-9d61-5c78d4413001";
		entity.position = {1.0f, 2.0f, 3.0f};
		entity.forceWireframe = true;
		document.entities.push_back(entity);

		std::string error;
		const bool saved = assets.SaveScene("roundtrip.scene.json", document, error);
		Require(saved, "scene save failed");
		const auto loaded = assets.LoadScene("roundtrip.scene.json");
		Require(static_cast<bool>(loaded), "scene load failed");
		Require(loaded.document.entities.size() == 1, "scene round-trip lost an entity");
		const auto& result = loaded.document.entities.front();
		Require(result.id == entity.id && result.name == entity.name, "scene round-trip changed durable entity data");
		Require(result.modelGuid == entity.modelGuid, "scene round-trip changed an asset GUID");
		Require(result.position == entity.position && result.forceWireframe, "scene round-trip changed entity settings");

		Require(!assets.SaveScene("../escaped.scene.json", document, error), "scene save escaped the project root");
	}

	void TestInvalidInputs(const std::filesystem::path& project) {
		Engine::AssetManager assets(project);
		std::string guid;
		std::string error;
		{
			std::ofstream invalid(project / "broken.gltf", std::ios::binary | std::ios::trunc);
			invalid << "{not valid JSON";
		}
		Require(!assets.ImportModel("broken.gltf", guid, error), "malformed glTF was accepted");
		Require(error.find("broken.gltf") != std::string::npos, "malformed glTF diagnostic omitted its source path");
		Require(!std::filesystem::exists(project / "broken.gltf.meta"), "failed import wrote a GUID sidecar");

		{
			std::ofstream invalidJson(project / "broken.scene.json", std::ios::binary | std::ios::trunc);
			invalidJson << "{not valid JSON";
		}
		const auto malformedScene = assets.LoadScene("broken.scene.json");
		Require(!malformedScene, "malformed scene JSON was accepted");
		Require(malformedScene.error.find("broken.scene.json") != std::string::npos,
			"malformed scene diagnostic omitted its source path");

		{
			std::ofstream unsupported(project / "future.scene.json", std::ios::binary | std::ios::trunc);
			unsupported << R"({"schemaVersion":99})";
		}
		Require(!assets.LoadScene("future.scene.json"), "unsupported future scene schema was accepted");
	}
}

int main() {
	TestWorkspace workspace;
	TestImportAndGuidReload(workspace.Project());
	TestDuplicateGuidAndProjectBoundary(workspace.Path(), workspace.Project());
	TestSceneJsonRoundTrip(workspace.Project());
	TestInvalidInputs(workspace.Project());
	return EXIT_SUCCESS;
}

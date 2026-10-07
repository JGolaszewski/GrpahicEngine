#include "assets/include/AssetManager.h"
#include "scene/include/Scene.h"
#include "rendering/include/renderer/Camera.h"
#include "rendering/include/renderer/Entity.h"
#include "rendering/include/renderer/Light.h"

#include <array>
#include <chrono>
#include <cstdint>
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
		const std::array<float, 9> positions = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
		const std::array<uint16_t, 3> indices = { 0, 1, 2 };
		std::ofstream buffer(path.parent_path() / "triangle.bin", std::ios::binary | std::ios::trunc);
		buffer.write(reinterpret_cast<const char*>(positions.data()), static_cast<std::streamsize>(sizeof(positions)));
		buffer.write(reinterpret_cast<const char*>(indices.data()), static_cast<std::streamsize>(sizeof(indices)));
		Require(static_cast<bool>(buffer), "could not write triangle buffer fixture");

		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"root","mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"buffers":[{"uri":"triangle.bin","byteLength":42}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";
		Require(static_cast<bool>(output), "could not write minimal glTF fixture");
	}

	void WriteMinimalPng(const std::filesystem::path& path) {
		constexpr std::array<uint8_t, 68> png = {
			0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
			0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,
			0x89,0x00,0x00,0x00,0x0b,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0x60,0x00,0x02,0x00,
			0x00,0x05,0x00,0x01,0xa5,0x2b,0x56,0x7b,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,
			0xae,0x42,0x60,0x82
		};
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
		Require(static_cast<bool>(output), "could not write PNG fixture");
	}

	void TestTextureImportAndRegistry(const std::filesystem::path& project) {
		WriteMinimalPng(project / "color.png");
		Engine::AssetManager first(project);
		std::string guid, error;
		Require(first.ImportTexture("color.png", guid, error), "texture import failed");
		Require(std::filesystem::exists(project / "color.png.meta"), "texture import did not write adjacent metadata");
		const auto image = first.GetTexture(guid);
		Require(image && image->width == 1 && image->height == 1 && image->rgba8.size() == 4,
			"texture import did not decode RGBA pixels");

		Engine::AssetManager reopened(project);
		std::vector<std::string> warnings;
		Require(reopened.RefreshRegistry(warnings, error), "asset registry refresh failed");
		Require(warnings.empty(), "valid texture metadata produced registry warnings");
		Require(reopened.GetTexture(guid) && reopened.GetKnownTextures().size() == 1,
			"texture was not discovered after reopening the project");

		std::filesystem::copy_file(project / "color.png.meta", project / "copied.png.meta");
		WriteMinimalPng(project / "copied.png");
		Engine::AssetManager duplicate(project);
		Require(!duplicate.ImportTexture("copied.png", guid, error) && error.find("duplicate asset GUID") != std::string::npos,
			"copied texture metadata did not report the duplicate GUID");
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
		Require(imported->meshes.size() == 1 && imported->meshes[0].primitives.size() == 1,
			"glTF mesh hierarchy was not imported");
		const auto& primitive = imported->meshes[0].primitives[0];
		Require(primitive.vertices.size() == 3 && primitive.indices == std::vector<uint32_t>({0, 1, 2}),
			"glTF primitive geometry was not decoded correctly");
		Require(primitive.boundsMin.x == 0.0f && primitive.boundsMax.x == 1.0f && primitive.boundsMax.y == 1.0f,
			"glTF primitive bounds were not calculated correctly");
		Require(imported->scenes.size() == 1 && imported->scenes[0] == std::vector<uint32_t>({0}),
			"glTF selected scene roots were not preserved");
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
		document.unitsPerMeter = 0.01f;
		Engine::SceneEntityDocument entity;
		entity.id = 17;
		entity.name = "serialized entity";
		entity.modelGuid = "e6f7e48a-1d3b-4f65-9d61-5c78d4413001";
		Engine::SceneMaterialDocument overrideMaterial;
		overrideMaterial.baseColor = {0.2f, 0.4f, 0.6f, 1.0f};
		overrideMaterial.metallic = 0.7f;
		overrideMaterial.brdfModel = "ggx";
		overrideMaterial.baseColorTextureGuid = "00000000-0000-4000-8000-000000000001";
		overrideMaterial.normalTextureGuid = "00000000-0000-4000-8000-000000000002";
		entity.materialOverrides.push_back(overrideMaterial);
		entity.position = {1.0f, 2.0f, 3.0f};
		entity.forceWireframe = true;
		document.entities.push_back(entity);

		Engine::SceneEntityDocument primitive;
		primitive.id = 23;
		primitive.name = "editable primitive";
		primitive.primitiveType = "sphere";
		Engine::SceneMaterialDocument primitiveMaterial;
		primitiveMaterial.baseColor = {0.8f, 0.3f, 0.1f, 1.0f};
		primitiveMaterial.roughness = 0.25f;
		primitive.material = primitiveMaterial;
		document.entities.push_back(primitive);

		Engine::SceneLightDocument light;
		light.id = 31;
		light.position = {3.0f, 4.0f, 5.0f};
		document.lights.push_back(light);
		Engine::SceneCameraDocument camera;
		camera.id = 41;
		camera.position = {6.0f, 7.0f, 8.0f};
		document.cameras.push_back(camera);

		std::string error;
		const bool saved = assets.SaveScene("roundtrip.scene.json", document, error);
		Require(saved, "scene save failed");
		const auto loaded = assets.LoadScene("roundtrip.scene.json");
		Require(static_cast<bool>(loaded), "scene load failed");
		Require(loaded.document.entities.size() == 2, "scene round-trip lost an entity");
		const auto& result = loaded.document.entities.front();
		Require(result.id == entity.id && result.name == entity.name, "scene round-trip changed durable entity data");
		Require(result.modelGuid == entity.modelGuid, "scene round-trip changed an asset GUID");
		Require(result.materialOverrides.size() == 1 && result.materialOverrides[0].has_value(),
			"scene round-trip lost a model material override");
		Require(result.materialOverrides[0]->baseColor == overrideMaterial.baseColor &&
			result.materialOverrides[0]->metallic == overrideMaterial.metallic &&
			result.materialOverrides[0]->baseColorTextureGuid == overrideMaterial.baseColorTextureGuid &&
			result.materialOverrides[0]->normalTextureGuid == overrideMaterial.normalTextureGuid,
			"scene round-trip changed an editable material override");
		Require(result.position == entity.position && result.forceWireframe, "scene round-trip changed entity settings");
		Require(loaded.document.unitsPerMeter == document.unitsPerMeter,
			"scene round-trip changed its physical unit scale");
		const auto& loadedPrimitive = loaded.document.entities[1];
		Require(loadedPrimitive.id == primitive.id && loadedPrimitive.primitiveType == primitive.primitiveType,
			"scene round-trip changed a primitive descriptor or durable ID");
		Require(loadedPrimitive.material && loadedPrimitive.material->baseColor == primitiveMaterial.baseColor &&
			loadedPrimitive.material->roughness == primitiveMaterial.roughness,
			"scene round-trip changed primitive material settings");
		Require(loaded.document.lights.size() == 1 && loaded.document.lights[0].id == light.id &&
			loaded.document.lights[0].position == light.position, "scene round-trip changed light state");
		Require(loaded.document.cameras.size() == 1 && loaded.document.cameras[0].id == camera.id &&
			loaded.document.cameras[0].position == camera.position, "scene round-trip changed camera state");

		Require(!assets.SaveScene("../escaped.scene.json", document, error), "scene save escaped the project root");
	}

	void TestStableSceneIds() {
		Engine::Scene scene;
		auto entity = std::make_shared<::Engine::Renderer::Entity>(nullptr, nullptr);
		Require(scene.AddEntity(entity, 17) == 17, "scene did not accept a persisted entity ID");
		Require(scene.AddEntity(std::make_shared<::Engine::Renderer::Entity>(nullptr, nullptr)) == 18,
			"runtime entity ID did not advance past a restored ID");
		Require(scene.AddEntity(std::make_shared<::Engine::Renderer::Entity>(nullptr, nullptr), 17) == Engine::InvalidEntityId,
			"scene accepted a duplicate entity ID");

		auto replacement = std::make_shared<::Engine::Renderer::Entity>(nullptr, nullptr);
		Require(scene.ReplaceEntity(17, replacement), "scene did not replace an entity by stable ID");
		Require(scene.FindEntity(17) == replacement && scene.GetEntityId(replacement.get()) == 17,
			"entity replacement did not preserve stable scene identity");
		Require(!scene.ReplaceEntity(999, std::make_shared<::Engine::Renderer::Entity>(nullptr, nullptr)),
			"scene replaced a missing entity ID");
		Require(!scene.ReplaceEntity(18, nullptr), "scene accepted a null replacement entity");

		Require(scene.AddLight(std::make_shared<::Engine::Renderer::SpotLight>(), 27) == 27,
			"scene did not accept a persisted light ID");
		Require(scene.AddLight(std::make_shared<::Engine::Renderer::SpotLight>()) == 28,
			"runtime light ID did not advance past a restored ID");
		Require(scene.AddLight(std::make_shared<::Engine::Renderer::SpotLight>(), 27) == Engine::InvalidLightId,
			"scene accepted a duplicate light ID");

		Require(scene.AddCamera(std::make_shared<::Engine::Renderer::Camera>(DirectX::XM_PIDIV4, 0.1f, 100.0f), 37) == 37,
			"scene did not accept a persisted camera ID");
		Require(scene.AddCamera(std::make_shared<::Engine::Renderer::Camera>(DirectX::XM_PIDIV4, 0.1f, 100.0f)) == 38,
			"runtime camera ID did not advance past a restored ID");
		Require(scene.AddCamera(std::make_shared<::Engine::Renderer::Camera>(DirectX::XM_PIDIV4, 0.1f, 100.0f), 37) == Engine::InvalidCameraId,
			"scene accepted a duplicate camera ID");
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
	TestTextureImportAndRegistry(workspace.Project());
	TestDuplicateGuidAndProjectBoundary(workspace.Path(), workspace.Project());
	TestSceneJsonRoundTrip(workspace.Project());
	TestStableSceneIds();
	TestInvalidInputs(workspace.Project());
	return EXIT_SUCCESS;
}

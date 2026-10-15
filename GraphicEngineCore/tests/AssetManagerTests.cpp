#include "assets/include/AssetManager.h"
#include "scene/include/Scene.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
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
    m_Path = std::filesystem::temp_directory_path() / ("GraphicEngineAssetManagerTests-" + std::to_string(nonce));
    std::filesystem::create_directories(m_Path / "project");
  }
  TestWorkspace(const TestWorkspace&) = delete;
  auto operator=(const TestWorkspace&) -> TestWorkspace& = delete;
  TestWorkspace(TestWorkspace&&) = delete;
  auto operator=(TestWorkspace&&) -> TestWorkspace& = delete;
  ~TestWorkspace() noexcept {
    std::error_code ignored;
    try {
      std::filesystem::remove_all(m_Path, ignored);
    } catch (...) {
      std::fputs("Could not remove test workspace\n", stderr);
    }
  }

  [[nodiscard]] auto Path() const noexcept -> const std::filesystem::path& { return m_Path; }
  [[nodiscard]] auto Project() const -> std::filesystem::path { return m_Path / "project"; }

private:
  std::filesystem::path m_Path;
};

void WriteMinimalModel(const std::filesystem::path& path) {
  const std::array<float, 9> positions = {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F};
  const std::array<uint16_t, 3> indices = {0, 1, 2};
  std::ofstream buffer(path.parent_path() / "triangle.bin", std::ios::binary | std::ios::trunc);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) // Binary fixtures are written as raw array bytes.
  buffer.write(reinterpret_cast<const char*>(positions.data()), static_cast<std::streamsize>(sizeof(positions)));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) // Binary fixtures are written as raw array bytes.
  buffer.write(reinterpret_cast<const char*>(indices.data()), static_cast<std::streamsize>(sizeof(indices)));
  Require(static_cast<bool>(buffer), "could not write triangle buffer fixture");

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output
      << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"root","mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"buffers":[{"uri":"triangle.bin","byteLength":42}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";
  Require(static_cast<bool>(output), "could not write minimal glTF fixture");
}

void WriteMinimalPng(const std::filesystem::path& path) {
  constexpr std::array<uint8_t, 68> png = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00,
      0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00,
      0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x00, 0x02, 0x00, 0x00, 0x05, 0x00,
      0x01, 0xa5, 0x2b, 0x56, 0x7b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
  };
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) // The PNG test fixture is serialized as raw bytes.
  output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  Require(static_cast<bool>(output), "could not write PNG fixture");
}

void TestTextureImportAndRegistry(const std::filesystem::path& project) {
  WriteMinimalPng(project / "color.png");
  Engine::AssetManager first(project);
  std::string guid;
  std::string error;
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
  Require(!duplicate.ImportTexture("copied.png", guid, error) && error.contains("duplicate asset GUID"),
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
  Require(imported->meshes.size() == 1 && imported->meshes.at(0).primitives.size() == 1,
          "glTF mesh hierarchy was not imported");
  const auto& primitive = imported->meshes.at(0).primitives.at(0);
  Require(primitive.vertices.size() == 3 && primitive.indices == std::vector<uint32_t>({0, 1, 2}),
          "glTF primitive geometry was not decoded correctly");
  Require(primitive.boundsMin.x == 0.0F && primitive.boundsMax.x == 1.0F && primitive.boundsMax.y == 1.0F,
          "glTF primitive bounds were not calculated correctly");
  Require(imported->scenes.size() == 1 && imported->scenes.at(0) == std::vector<uint32_t>({0}),
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
  Require(error.contains("duplicate asset GUID"), "duplicate GUID error lacks the conflicting identity");
  std::filesystem::remove(project / "copy.gltf");
  std::filesystem::remove(project / "copy.gltf.meta");

  Engine::AssetManager boundary(project);
  Require(!boundary.ImportModel(workspace / "outside.gltf", guid, error),
          "asset outside the project root was accepted");
}

void TestSceneJsonRoundTrip(const std::filesystem::path& project) {
  Engine::AssetManager assets(project);
  Engine::SceneDocument document;
  document.unitsPerMeter = 0.01F;
  Engine::SceneEntityDocument entity;
  entity.id = 17;
  entity.name = "serialized entity";
  entity.modelGuid = "e6f7e48a-1d3b-4f65-9d61-5c78d4413001";
  Engine::SceneMaterialDocument overrideMaterial;
  overrideMaterial.baseColor = {0.2F, 0.4F, 0.6F, 1.0F};
  overrideMaterial.metallic = 0.7F;
  overrideMaterial.brdfModel = "ggx";
  overrideMaterial.baseColorTextureGuid = "00000000-0000-4000-8000-000000000001";
  overrideMaterial.normalTextureGuid = "00000000-0000-4000-8000-000000000002";
  entity.materialOverrides.emplace_back(overrideMaterial);
  entity.position = {1.0F, 2.0F, 3.0F};
  entity.forceWireframe = true;
  document.entities.push_back(entity);

  Engine::SceneEntityDocument primitive;
  primitive.id = 23;
  primitive.name = "editable primitive";
  primitive.primitiveType = "sphere";
  Engine::SceneMaterialDocument primitiveMaterial;
  primitiveMaterial.baseColor = {0.8F, 0.3F, 0.1F, 1.0F};
  primitiveMaterial.roughness = 0.25F;
  primitive.material = primitiveMaterial;
  document.entities.push_back(primitive);

  Engine::SceneLightDocument light;
  light.id = 31;
  light.position = {3.0F, 4.0F, 5.0F};
  document.lights.push_back(light);
  Engine::SceneCameraDocument camera;
  camera.id = 41;
  camera.position = {6.0F, 7.0F, 8.0F};
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
  Require(result.materialOverrides.size() == 1 && result.materialOverrides.at(0).has_value(),
          "scene round-trip lost a model material override");
  const Engine::SceneMaterialDocument loadedOverride =
      result.materialOverrides.at(0).value_or(Engine::SceneMaterialDocument{});
  Require(loadedOverride.baseColor == overrideMaterial.baseColor &&
              loadedOverride.metallic == overrideMaterial.metallic &&
              loadedOverride.baseColorTextureGuid == overrideMaterial.baseColorTextureGuid &&
              loadedOverride.normalTextureGuid == overrideMaterial.normalTextureGuid,
          "scene round-trip changed an editable material override");
  Require(result.position == entity.position && result.forceWireframe, "scene round-trip changed entity settings");
  Require(loaded.document.unitsPerMeter == document.unitsPerMeter, "scene round-trip changed its physical unit scale");
  const auto& loadedPrimitive = loaded.document.entities.at(1);
  Require(loadedPrimitive.id == primitive.id && loadedPrimitive.primitiveType == primitive.primitiveType,
          "scene round-trip changed a primitive descriptor or durable ID");
  Require(loadedPrimitive.material && loadedPrimitive.material->baseColor == primitiveMaterial.baseColor &&
              loadedPrimitive.material->roughness == primitiveMaterial.roughness,
          "scene round-trip changed primitive material settings");
  Require(loaded.document.lights.size() == 1 && loaded.document.lights.at(0).id == light.id &&
              loaded.document.lights.at(0).position == light.position,
          "scene round-trip changed light state");
  Require(loaded.document.cameras.size() == 1 && loaded.document.cameras.at(0).id == camera.id &&
              loaded.document.cameras.at(0).position == camera.position,
          "scene round-trip changed camera state");

  Require(!assets.SaveScene("../escaped.scene.json", document, error), "scene save escaped the project root");
}

void TestStableSceneIds() {
  Engine::Scene scene;
  (void)scene.QueueCreate("Persisted entity");
  const auto created = scene.sync();
  Require(created.size() == 1 && created.front().Outcome.has_value(), "scene did not apply a staged entity creation");
  const Engine::EntityHandle entity = created.front().Entity;
  constexpr Engine::Ecs::SceneGuid persistedGuid{.High = 0, .Low = 17};
  (void)scene.QueueSet(entity, Engine::Ecs::PersistentIdentity{.Guid = persistedGuid});
  const auto updated = scene.sync();
  Require(updated.size() == 1 && updated.front().Outcome.has_value(),
          "scene did not apply a staged persistent identity component");
  const auto* identity = scene.World().Get<Engine::Ecs::PersistentIdentity>(entity);
  Require(identity != nullptr && identity->Guid == persistedGuid,
          "scene did not preserve the persistent identity in the ECS component store");
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
  Require(error.contains("broken.gltf"), "malformed glTF diagnostic omitted its source path");
  Require(!std::filesystem::exists(project / "broken.gltf.meta"), "failed import wrote a GUID sidecar");

  {
    std::ofstream invalidJson(project / "broken.scene.json", std::ios::binary | std::ios::trunc);
    invalidJson << "{not valid JSON";
  }
  const auto malformedScene = assets.LoadScene("broken.scene.json");
  Require(!malformedScene, "malformed scene JSON was accepted");
  Require(malformedScene.error.contains("broken.scene.json"), "malformed scene diagnostic omitted its source path");

  {
    std::ofstream unsupported(project / "future.scene.json", std::ios::binary | std::ios::trunc);
    unsupported << R"({"schemaVersion":99})";
  }
  Require(!assets.LoadScene("future.scene.json"), "unsupported future scene schema was accepted");
}
} // namespace

auto main() -> int {
  try {
    TestWorkspace workspace;
    TestImportAndGuidReload(workspace.Project());
    TestTextureImportAndRegistry(workspace.Project());
    TestDuplicateGuidAndProjectBoundary(workspace.Path(), workspace.Project());
    TestSceneJsonRoundTrip(workspace.Project());
    TestStableSceneIds();
    TestInvalidInputs(workspace.Project());
    return EXIT_SUCCESS;
  } catch (...) {
    std::fputs("Asset manager tests failed\n", stderr);
    return EXIT_FAILURE;
  }
}

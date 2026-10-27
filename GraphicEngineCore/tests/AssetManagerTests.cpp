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

void WriteMinimalHdr(const std::filesystem::path& path) {
  constexpr std::array<uint8_t, 8> rgbePixels = {128, 64, 32, 130, 0, 128, 128, 129};
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) // Radiance fixture stores encoded RGBE bytes.
  output.write(reinterpret_cast<const char*>(rgbePixels.data()), static_cast<std::streamsize>(rgbePixels.size()));
  Require(static_cast<bool>(output), "could not write Radiance HDR fixture");
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

void TestEnvironmentImportAndRegistry(const std::filesystem::path& project) {
  std::filesystem::create_directories(project);
  WriteMinimalHdr(project / "studio.hdr");
  WriteMinimalPng(project / "color.png");
  Engine::AssetManager first(project);
  std::string guid;
  std::string error;
  Require(first.ImportEnvironment("studio.hdr", guid, error), "HDR environment import failed");
  Require(std::filesystem::exists(project / "studio.hdr.meta"), "HDR import did not write its metadata sidecar");
  const auto environment = first.GetEnvironment(guid);
  Require(environment && environment->width == 2 && environment->height == 1 && environment->rgb32f.size() == 6,
          "HDR import did not produce tightly packed RGB32F environment data");
  Require(environment->rgb32f.at(0) > 1.0F, "HDR import clamped a sample above one");
  Require(first.GetKnownEnvironments().size() == 1, "HDR environment was not listed in the asset registry");

  Engine::AssetManager reopened(project);
  std::vector<std::string> warnings;
  Require(reopened.RefreshRegistry(warnings, error), "environment registry refresh failed");
  Require(warnings.empty(), "valid environment metadata produced registry warnings");
  const auto reloaded = reopened.GetEnvironment(guid);
  Require(reloaded && reloaded->rgb32f == environment->rgb32f,
          "environment was not decoded again from its GUID sidecar");
  Require(reopened.RenameAsset(guid, "renamed-environment", error), "registered environment rename failed");
  Require(std::filesystem::exists(project / "renamed-environment.hdr.meta") &&
              reopened.GetKnownEnvironments().front().guid == guid,
          "renaming an environment did not preserve its sidecar identity");

  std::ofstream invalid(project / "invalid.hdr", std::ios::binary | std::ios::trunc);
  invalid << "not a Radiance image";
  invalid.close();
  Require(!reopened.ImportEnvironment("invalid.hdr", guid, error) && !error.empty(),
          "invalid HDR source did not fail with a diagnostic");
  Require(!std::filesystem::exists(project / "invalid.hdr.meta"), "failed HDR import wrote a metadata sidecar");
  Require(!reopened.ImportEnvironment("color.png", guid, error) && error.contains("Radiance HDR"),
          "non-HDR extension was accepted as an environment");
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

void TestAssetRenamePreservesGuidAndSceneReferences(const std::filesystem::path& project) {
  const auto renameProject = project.parent_path() / "rename-test-project";
  std::filesystem::create_directories(renameProject);
  WriteMinimalModel(renameProject / "model.gltf");
  Engine::AssetManager assets(renameProject);
  std::string guid;
  std::string error;
  Require(assets.ImportModel("model.gltf", guid, error), "model import for rename test failed");

  Engine::SceneDocument document;
  Engine::SceneEntityDocument entity;
  entity.id = 1;
  entity.modelGuid = guid;
  document.entities.push_back(entity);
  Require(assets.SaveScene("reference.scene.json", document, error), "could not save model reference fixture");

  std::ifstream metadataInput(renameProject / "model.gltf.meta", std::ios::binary);
  const std::string metadataBefore((std::istreambuf_iterator<char>(metadataInput)), std::istreambuf_iterator<char>());
  metadataInput.close();
  const bool renameSucceeded = assets.RenameAsset(guid, "renamed", error);
  Require(renameSucceeded, ("asset rename failed: " + error).c_str());
  Require(!std::filesystem::exists(renameProject / "model.gltf") &&
              !std::filesystem::exists(renameProject / "model.gltf.meta"),
          "asset rename left the previous source or metadata file behind");
  Require(std::filesystem::exists(renameProject / "renamed.gltf") &&
              std::filesystem::exists(renameProject / "renamed.gltf.meta"),
          "asset rename did not move both source and metadata files");
  Require(assets.GetSourcePath(guid) == renameProject / "renamed.gltf",
          "asset rename changed or lost its registered GUID");
  const auto knownModels = assets.GetKnownModels();
  Require(knownModels.size() == 1 && knownModels.front().guid == guid && knownModels.front().name == "renamed",
          "asset registry did not update the renamed asset's display name");

  std::ifstream renamedMetadataInput(renameProject / "renamed.gltf.meta", std::ios::binary);
  const std::string metadataAfter((std::istreambuf_iterator<char>(renamedMetadataInput)),
                                  std::istreambuf_iterator<char>());
  Require(metadataBefore == metadataAfter, "asset rename changed persistent metadata identity");
  const auto loadedReference = assets.LoadScene("reference.scene.json");
  Require(loadedReference && loadedReference.document.entities.front().modelGuid == guid,
          "scene reference no longer points to the renamed asset GUID");

  std::ofstream(renameProject / "occupied.gltf") << "occupied";
  Require(!assets.RenameAsset(guid, "occupied", error) && !error.empty(),
          "asset rename overwrote an existing destination");
  Require(std::filesystem::exists(renameProject / "renamed.gltf") &&
              assets.GetSourcePath(guid) == renameProject / "renamed.gltf",
          "failed asset rename changed the registered asset state");
  Require(!assets.RenameAsset(guid, "../outside", error) && !error.empty(),
          "asset rename accepted a path traversal name");

  WriteMinimalPng(renameProject / "color.png");
  std::string textureGuid;
  Require(assets.ImportTexture("color.png", textureGuid, error), "texture import for rename test failed");
  std::ifstream textureMetadataInput(renameProject / "color.png.meta", std::ios::binary);
  const std::string textureMetadataBefore((std::istreambuf_iterator<char>(textureMetadataInput)),
                                          std::istreambuf_iterator<char>());
  textureMetadataInput.close();
  Engine::SceneMaterialDocument material;
  material.baseColorTextureGuid = textureGuid;
  document.entities.front().material = material;
  Require(assets.SaveScene("texture-reference.scene.json", document, error),
          "could not save texture reference fixture");
  Require(assets.RenameAsset(textureGuid, "albedo", error), "texture rename failed");
  Require(assets.GetSourcePath(textureGuid) == renameProject / "albedo.png" &&
              std::filesystem::exists(renameProject / "albedo.png.meta"),
          "texture rename did not preserve its registered identity and metadata");
  const auto knownTextures = assets.GetKnownTextures();
  Require(knownTextures.size() == 1 && knownTextures.front().guid == textureGuid &&
              knownTextures.front().name == "albedo",
          "texture registry did not update the renamed asset's display name");
  std::ifstream renamedTextureMetadataInput(renameProject / "albedo.png.meta", std::ios::binary);
  const std::string textureMetadataAfter((std::istreambuf_iterator<char>(renamedTextureMetadataInput)),
                                         std::istreambuf_iterator<char>());
  Require(textureMetadataBefore == textureMetadataAfter, "texture rename changed persistent metadata identity");
  const auto loadedTextureReference = assets.LoadScene("texture-reference.scene.json");
  Require(loadedTextureReference && loadedTextureReference.document.entities.front().material &&
              loadedTextureReference.document.entities.front().material->baseColorTextureGuid == textureGuid,
          "material texture reference no longer resolves to the renamed texture GUID");
  Require(assets.GetTexture(textureGuid) != nullptr, "texture rename invalidated its decoded image cache");
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
  entity.materialGuid = "00000000-0000-4000-8000-000000000003";
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
  Require(result.materialGuid == entity.materialGuid, "scene round-trip changed a reusable material GUID");
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

void TestReusableMaterialAssets(const std::filesystem::path& project) {
  const auto materialProject = project / "material-assets";
  std::filesystem::create_directories(materialProject);
  Engine::AssetManager assets(materialProject);
  Engine::SceneMaterialDocument source;
  source.baseColor = {0.2F, 0.4F, 0.6F, 1.0F};
  source.metallic = 0.7F;
  source.roughness = 0.3F;
  std::string error;
  const auto created = assets.CreateMaterial("Shared", source);
  Require(created.has_value(), "material asset creation failed");
  const auto& guid = *created;
  Require(assets.GetMaterial(guid) == source, "new material asset is not cached with its authored properties");
  auto edited = source;
  edited.roughness = 0.8F;
  Require(assets.SaveMaterial(guid, edited, error) && assets.GetMaterial(guid) == edited,
          "material asset property edits were not persisted");

  Engine::SceneDocument scene;
  for (uint64_t id = 1; id <= 2; ++id) {
    Engine::SceneEntityDocument entity;
    entity.id = id;
    entity.name = "mesh " + std::to_string(id);
    entity.modelGuid = "e6f7e48a-1d3b-4f65-9d61-5c78d4413001";
    entity.materialGuid = guid;
    scene.entities.push_back(entity);
  }
  Require(assets.SaveScene("shared.scene.json", scene, error), "could not save shared material references");
  const auto loadedScene = assets.LoadScene("shared.scene.json");
  Require(loadedScene && loadedScene.document.entities.size() == 2 &&
              loadedScene.document.entities[0].materialGuid == guid &&
              loadedScene.document.entities[1].materialGuid == guid,
          "scene round-trip did not preserve shared material references");

  Require(assets.RenameAsset(guid, "RenamedShared", error), "material asset rename failed");
  Require(assets.GetKnownMaterials().size() == 1 && assets.GetKnownMaterials().front().guid == guid &&
              assets.GetKnownMaterials().front().name == "RenamedShared",
          "renaming a material changed or lost its stable identity");
  const auto persistedScene = assets.LoadScene("shared.scene.json");
  Require(persistedScene && persistedScene.document.entities.front().materialGuid == guid,
          "renaming a material invalidated existing scene references");

  Engine::AssetManager reopened(materialProject);
  std::vector<std::string> warnings;
  Require(reopened.RefreshRegistry(warnings, error) && warnings.empty(), "material asset registry reload failed");
  Require(reopened.GetKnownMaterials().size() == 1 && reopened.LoadMaterial(guid, error) &&
              reopened.GetMaterial(guid) == edited,
          "material asset did not load in a later manager session");
  Require(!reopened.LoadMaterial("not-a-guid", error) && error.contains("invalid"),
          "invalid material identity did not produce a useful diagnostic");

  const auto materialPath = materialProject / reopened.GetKnownMaterials().front().sourcePath;
  std::ofstream(materialPath, std::ios::binary | std::ios::trunc) << "{not valid JSON";
  Engine::AssetManager corruptedAsset(materialProject);
  Require(!corruptedAsset.LoadMaterial(guid, error) && error.contains("invalid material asset document") &&
              error.contains(materialPath.generic_string()),
          "malformed registered material did not identify the invalid asset document");
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
    TestAssetRenamePreservesGuidAndSceneReferences(workspace.Project());
    TestTextureImportAndRegistry(workspace.Project());
    TestEnvironmentImportAndRegistry(workspace.Project() / "environment-test");
    TestDuplicateGuidAndProjectBoundary(workspace.Path(), workspace.Project());
    TestSceneJsonRoundTrip(workspace.Project());
    TestReusableMaterialAssets(workspace.Project());
    TestStableSceneIds();
    TestInvalidInputs(workspace.Project());
    return EXIT_SUCCESS;
  } catch (...) {
    std::fputs("Asset manager tests failed\n", stderr);
    return EXIT_FAILURE;
  }
}
